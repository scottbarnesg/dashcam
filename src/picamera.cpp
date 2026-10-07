// Pi Camera Module capture backend. Only compiled when configured with -DUSE_PI_CAMERA=ON.
//
// Implemented per the Raspberry Pi camera software documentation: rpicam-vid supports
// "--codec mjpeg" writing an MJPEG (raw JPEG stream) to stdout when given "-o -".
// We spawn the process and decode each JPEG from the stream with cv::imdecode.
// Docs: https://www.raspberrypi.com/documentation/computers/camera_software.html#rpicam-vid

#include "camera_impl.hpp"

#ifdef USE_PI_CAMERA

#include <algorithm>
#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <unistd.h>

namespace {
constexpr int kPiWidth = 1280;
constexpr int kPiHeight = 720;
constexpr int kPiFps = 20;
}

PiCamera::PiCamera() {
    std::array<char, 512> cmd;
    std::snprintf(cmd.data(), cmd.size(),
                  "rpicam-vid -t 0 -o - --codec mjpeg --width %d --height %d --framerate %d --quality 85 2>/dev/null",
                  kPiWidth, kPiHeight, kPiFps);
    pipe = popen(cmd.data(), "r");
    if (!pipe) {
        throw std::runtime_error("Could not start rpicam-vid");
    }
    if (!pump()) {
        throw std::runtime_error("rpicam-vid produced no video (is the camera enabled in raspi-config?)");
    }
}

PiCamera::~PiCamera() {
    if (pipe) {
        pclose(pipe);
    }
}

// Read bytes until at least one complete JPEG (SOI..EOI) is available in buffer.
bool PiCamera::pump() {
    static const unsigned char kJpegStart[3] = {0xFF, 0xD8, 0xFF};
    static const unsigned char kJpegEnd[2] = {0xFF, 0xD9};
    unsigned char chunk[16384];
    for (;;) {
        auto begin = buffer.data();
        std::size_t size = buffer.size();
        // Already have a complete JPEG?
        if (size >= 5) {
            const unsigned char* soi = std::search(begin, begin + size, kJpegStart, kJpegStart + 3);
            if (soi != begin + size) {
                const unsigned char* eoi = std::search(soi + 3, begin + size, kJpegEnd, kJpegEnd + 2);
                if (eoi != begin + size) {
                    return true;
                }
            }
        }
        std::size_t n = std::fread(chunk, 1, sizeof(chunk), pipe);
        if (n == 0) {
            return false;
        }
        buffer.insert(buffer.end(), chunk, chunk + n);
    }
}

cv::Mat PiCamera::captureImage() {
    static const unsigned char kJpegStart[3] = {0xFF, 0xD8, 0xFF};
    static const unsigned char kJpegEnd[2] = {0xFF, 0xD9};
    if (!pump()) {
        return cv::Mat();
    }
    const unsigned char* begin = buffer.data();
    const unsigned char* end = begin + buffer.size();
    const unsigned char* soi = std::search(begin, end, kJpegStart, kJpegStart + 3);
    const unsigned char* eoi = std::search(soi + 3, end, kJpegEnd, kJpegEnd + 2);
    const std::size_t consumed = (eoi + 2) - begin;
    std::vector<unsigned char> jpeg;
    jpeg.assign(buffer.begin(), buffer.begin() + consumed);
    buffer.erase(buffer.begin(), buffer.begin() + consumed);
    return cv::imdecode(jpeg, cv::IMREAD_COLOR);
}

#endif
