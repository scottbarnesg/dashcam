#include <chrono>
#include <memory>

#include <opencv2/core.hpp>

#ifndef FRAME_HPP
#define FRAME_HPP

// Pixel layout of RawFrame::data. NV12/I420 mats are the contiguous,
// unpadded "packed" form OpenCV expects: (3H/2) x W, CV_8UC1, Y plane
// first. BGR mats are H x W, CV_8UC3.
enum class PixelFormat { BGR, NV12, I420 };

// Color space tag (mirrors what the libcamera stream reports) so the
// hardware encoder can annotate its input; keeps libcamera types out of
// the rest of the pipeline.
enum class ColorSpace { Unspecified, Rec709, Smpte170m };

// One captured frame as delivered by a Camera backend, before any
// color conversion. Recording and motion detection consume the native
// YUV formats directly; only display/software-encode paths convert to BGR.
struct RawFrame {
    cv::Mat data;
    PixelFormat format = PixelFormat::BGR;
    ColorSpace colorSpace = ColorSpace::Unspecified;
    std::chrono::system_clock::time_point timestamp{};

    bool empty() const { return data.empty(); }
    cv::Size size() const {
        switch (format) {
            case PixelFormat::NV12:
            case PixelFormat::I420:
                return cv::Size(data.cols, data.rows * 2 / 3);
            case PixelFormat::BGR:
                return data.size();
        }
        return data.size();
    }
};

// Frames flow through the pipeline as refcounted leases on pooled buffers
// (see frame_pool.hpp): the camera hands out leases, each stage holds the
// ones it references, and the buffer returns to the pool when the last
// lease is released.
using SharedFrame = std::shared_ptr<const RawFrame>;

#endif
