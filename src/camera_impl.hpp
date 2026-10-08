#include "camera.hpp"

#include <memory>
#include <stdexcept>

#ifndef CAMERA_IMPL_H
#define CAMERA_IMPL_H

// Synthetic camera for tests lives in tests/simulated_camera.hpp (test-only).

// Captures from the Pi Camera Module using libcamera directly, following the
// libcamera Application Writer's Guide (docs.libcamera.org). Uses the
// VideoRecording stream role, maps completed frame buffers, and converts
// NV12 / YUV420 / MJPEG output to BGR Mats. Requires the libcamera-dev
// package.
class PiCamera : public Camera {
    public:
        PiCamera();
        ~PiCamera() override;
        cv::Mat captureImage() override;
        std::string name() const override { return "pi"; }
    private:
        class Impl; // All libcamera types live in picamera.cpp.
        std::unique_ptr<Impl> impl;
};

#endif
