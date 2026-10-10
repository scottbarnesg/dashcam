#include "orientation.hpp"

#include <map>

namespace {

// Flip (horizontal) first, then rotate; matches the Mirror* convention in
// orientation.hpp. Works on any single- or multi-channel plane.
cv::Mat transformPlane(const cv::Mat& plane, const OrientationTransform& transform) {
    cv::Mat result = plane;
    if (transform.mirror) {
        cv::flip(result, result, 1);
    }
    if (transform.rotateCode >= 0) {
        cv::rotate(result, result, transform.rotateCode);
    }
    return result;
}

bool applyBgr(RawFrame& frame, const OrientationTransform& transform) {
    frame.data = transformPlane(frame.data, transform);
    return true;
}

// Rotates/flips the Y plane and the chroma plane(s) of a packed NV12 or
// I420 frame (frame.hpp layout) and reassembles the packed form. The same
// per-plane transform yields a correct whole-image transform for 4:2:0
// subsampling: the rotated chroma plane always matches half the rotated
// luma dimensions.
bool applyYuv(RawFrame& frame, const OrientationTransform& transform, bool semiPlanar) {
    const cv::Mat& packed = frame.data;
    if (packed.rows % 3 != 0) {
        return false;
    }
    const int width = packed.cols;
    const int height = packed.rows * 2 / 3;
    if (width % 2 || height % 2) {
        return false; // Odd dimensions cannot be 4:2:0 subsampled.
    }

    cv::Mat y = transformPlane(packed.rowRange(0, height), transform);
    const int newWidth = y.cols;
    const int newHeight = y.rows;
    if (newWidth % 2 || newHeight % 2) {
        return false;
    }
    cv::Mat out(newHeight * 3 / 2, newWidth, CV_8UC1);
    y.copyTo(out.rowRange(0, newHeight));

    if (semiPlanar) {
        cv::Mat uv = packed.rowRange(height, height + height / 2).reshape(2, height / 2);
        cv::Mat uv2 = transformPlane(uv, transform);
        uv2.reshape(1, newHeight / 2).copyTo(out.rowRange(newHeight, newHeight + newHeight / 2));
    } else {
        unsigned char* chroma = packed.data + (std::size_t)height * width;
        cv::Mat u(height / 2, width / 2, CV_8UC1, chroma);
        cv::Mat v(height / 2, width / 2, CV_8UC1, chroma + (std::size_t)(width / 2) * (height / 2));
        cv::Mat u2 = transformPlane(u, transform);
        cv::Mat v2 = transformPlane(v, transform);
        uchar* outChroma = out.data + (std::size_t)newHeight * newWidth;
        cv::Mat uDst(newHeight / 2, newWidth / 2, CV_8UC1, outChroma);
        cv::Mat vDst(newHeight / 2, newWidth / 2, CV_8UC1, outChroma + (std::size_t)(newWidth / 2) * (newHeight / 2));
        u2.copyTo(uDst);
        v2.copyTo(vDst);
    }
    frame.data = std::move(out);
    return true;
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

bool applyOrientation(RawFrame& frame, CameraOrientation orientation) {
    OrientationTransform transform = transformFor(orientation);
    if (frame.empty() || transform.identity()) {
        return true;
    }
    switch (frame.format) {
        case PixelFormat::BGR:
            return applyBgr(frame, transform);
        case PixelFormat::NV12:
            return applyYuv(frame, transform, true);
        case PixelFormat::I420:
            return applyYuv(frame, transform, false);
    }
    return false;
}
