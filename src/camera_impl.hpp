#include "camera.hpp"

#include <stdexcept>

#include <opencv2/videoio.hpp>

#ifndef CAMERA_IMPL_H
#define CAMERA_IMPL_H

class USBCamera : public Camera {
    public:
        USBCamera();
        ~USBCamera() override;
        cv::Mat captureImage() override;
        std::string name() const override { return "usb"; }
    private:
        cv::VideoCapture cam;
        int cameraIndex = 0;
};

// Synthetic camera for tests lives in tests/simulated_camera.hpp (test-only).

#ifdef USE_PI_CAMERA
// Captures from the Pi Camera Module using libcamera directly, following the
// libcamera Application Writer's Guide (docs.libcamera.org). Uses the
// VideoRecording stream role, maps completed frame buffers, and converts
// NV12 / YUV420 / MJPEG output to BGR Mats. Requires -DUSE_PI_CAMERA=ON and
// the libcamera-dev package.
class PiCamera : public Camera {
    public:
        PiCamera();
        ~PiCamera() override;
        cv::Mat captureImage() override;
        std::string name() const override { return "pi"; }
    private:
        class Impl; // All libcamera types live in picamera.cpp.
        Impl* impl = nullptr;
};
#endif

#endif
