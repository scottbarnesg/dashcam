#include "orientation.hpp"

#include <map>

namespace {

// Flip (horizontal) first, then rotate; matches the Mirror* convention in
// orientation.hpp. Works on any single- or multi-channel plane. Results
// land in `scratch`, which OpenCV's create() keeps allocated across calls
// when geometry repeats, so steady-state calls allocate nothing. cv::flip
// is in-place safe; the mirror+rotate chain mirrors what the old
// allocating transformPlane did.
void transformInto(const cv::Mat& plane, const OrientationTransform& transform, cv::Mat& scratch) {
    if (transform.mirror && transform.rotateCode >= 0) {
        cv::flip(plane, scratch, 1);
        cv::rotate(scratch, scratch, transform.rotateCode);
    } else if (transform.mirror) {
        cv::flip(plane, scratch, 1);
    } else {
        cv::rotate(plane, scratch, transform.rotateCode);
    }
}

} // namespace

bool parseCameraOrientation(const std::string& value, CameraOrientation& out) {
    static const std::map<std::string, CameraOrientation> table{
        {"auto", CameraOrientation::Auto},
        {"0", CameraOrientation::Rotate0},
        {"90", CameraOrientation::Rotate90},
        {"180", CameraOrientation::Rotate180},
        {"270", CameraOrientation::Rotate270},
        {"mirror-0", CameraOrientation::Mirror0},
        {"mirror-90", CameraOrientation::Mirror90},
        {"mirror-180", CameraOrientation::Mirror180},
        {"mirror-270", CameraOrientation::Mirror270},
    };
    auto entry = table.find(value);
    if (entry == table.end()) {
        return false;
    }
    out = entry->second;
    return true;
}

OrientationTransform transformFor(CameraOrientation orientation) {
    switch (orientation) {
        case CameraOrientation::Rotate90:
            return {false, cv::ROTATE_90_CLOCKWISE};
        case CameraOrientation::Rotate180:
            return {false, cv::ROTATE_180};
        case CameraOrientation::Rotate270:
            return {false, cv::ROTATE_90_COUNTERCLOCKWISE};
        case CameraOrientation::Mirror0:
            return {true, -1};
        case CameraOrientation::Mirror90:
            return {true, cv::ROTATE_90_CLOCKWISE};
        case CameraOrientation::Mirror180:
            return {true, cv::ROTATE_180};
        case CameraOrientation::Mirror270:
            return {true, cv::ROTATE_90_COUNTERCLOCKWISE};
        case CameraOrientation::Auto:    // Resolved by the caller; pass-through.
        case CameraOrientation::Rotate0: // Fall through
            break;
    }
    return {};
}

CameraOrientation orientationFromRotationProperty(int ccwDegrees) {
    int normalized = ccwDegrees % 360;
    if (normalized < 0) {
        normalized += 360;
    }
    // Sensors report multiples of 90 today; anything else snaps to the
    // closest one (a fractional rotation is not representable pixel-wise).
    int clockwise = (360 - normalized) % 360;
    int snapped = ((clockwise + 45) / 90) % 4 * 90;
    switch (snapped) {
        case 90:
            return CameraOrientation::Rotate90;
        case 180:
            return CameraOrientation::Rotate180;
        case 270:
            return CameraOrientation::Rotate270;
        default:
            return CameraOrientation::Rotate0;
    }
}

bool applyOrientationInto(const RawFrame& src, RawFrame& dst,
                          CameraOrientation orientation,
                          cv::Mat& scratchY, cv::Mat& scratchC) {
    OrientationTransform transform = transformFor(orientation);
    if (src.empty()) {
        return false;
    }
    if (transform.identity()) {
        src.data.copyTo(dst.data);
        dst.format = src.format;
        return true;
    }
    if (src.format == PixelFormat::BGR) {
        transformInto(src.data, transform, scratchY);
        scratchY.copyTo(dst.data);
        dst.format = src.format;
        return true;
    }
    // Packed NV12/I420: transform the Y plane and the chroma plane(s)
    // separately and reassemble. The same per-plane transform yields a
    // correct whole-image transform for 4:2:0 subsampling: the rotated
    // chroma plane always matches half the rotated luma dimensions.
    const cv::Mat& packed = src.data;
    if (packed.rows % 3 != 0) {
        return false;
    }
    const int width = packed.cols;
    const int height = packed.rows * 2 / 3;
    if (width % 2 || height % 2) {
        return false; // Odd dimensions cannot be 4:2:0 subsampled.
    }
    const bool semiPlanar = src.format == PixelFormat::NV12;
    transformInto(packed.rowRange(0, height), transform, scratchY);
    const int newWidth = scratchY.cols;
    const int newHeight = scratchY.rows;
    if (newWidth % 2 || newHeight % 2) {
        return false;
    }
    dst.data.create(newHeight * 3 / 2, newWidth, CV_8UC1);
    scratchY.copyTo(dst.data.rowRange(0, newHeight));
    if (semiPlanar) {
        cv::Mat uv = packed.rowRange(height, height + height / 2).reshape(2, height / 2);
        transformInto(uv, transform, scratchC);
        // View, not mutation: reshaping scratchC itself would change its
        // geometry and force a reallocation on the next call.
        cv::Mat chromaView = scratchC.reshape(1, newHeight / 2);
        chromaView.copyTo(dst.data.rowRange(newHeight, newHeight + newHeight / 2));
    } else {
        const unsigned char* chroma = packed.data + (std::size_t)height * width;
        const cv::Mat u(height / 2, width / 2, CV_8UC1, const_cast<unsigned char*>(chroma));
        const cv::Mat v(height / 2, width / 2, CV_8UC1,
                        const_cast<unsigned char*>(chroma + (std::size_t)(width / 2) * (height / 2)));
        unsigned char* outChroma = dst.data.data + (std::size_t)newHeight * newWidth;
        cv::Mat uDst(newHeight / 2, newWidth / 2, CV_8UC1, outChroma);
        cv::Mat vDst(newHeight / 2, newWidth / 2, CV_8UC1,
                     outChroma + (std::size_t)(newWidth / 2) * (newHeight / 2));
        transformInto(u, transform, scratchC);
        scratchC.copyTo(uDst);
        transformInto(v, transform, scratchC);
        scratchC.copyTo(vDst);
    }
    dst.format = src.format;
    return true;
}

bool applyOrientation(RawFrame& frame, CameraOrientation orientation) {
    OrientationTransform transform = transformFor(orientation);
    if (frame.empty() || transform.identity()) {
        return true;
    }
    // Allocating convenience wrapper (tests, dev paths). The capture hot
    // path uses applyOrientationInto with caller-owned scratch buffers.
    cv::Mat scratchY, scratchC;
    RawFrame out;
    out.colorSpace = frame.colorSpace;
    out.timestamp = frame.timestamp;
    if (!applyOrientationInto(frame, out, orientation, scratchY, scratchC)) {
        return false;
    }
    frame = std::move(out);
    return true;
}
