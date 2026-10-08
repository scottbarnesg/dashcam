#include <memory>
#include <string>

#include <opencv2/core.hpp>

#ifndef CAMERA_H
#define CAMERA_H

// Abstract capture interface. Production backend: PiCamera (libcamera).
// Tests use SimulatedCamera (tests/simulated_camera.hpp).
class Camera {
    public:
        virtual ~Camera() = default;
        // Returns the next frame. Empty Mat on failure; callers must check frame.empty().
        virtual cv::Mat captureImage() = 0;
        virtual std::string name() const = 0;
};

// Factory: "pi" (libcamera).
std::unique_ptr<Camera> createCamera(const std::string& backend);

// Captures from the Pi Camera Module using libcamera directly, following the
// libcamera Application Writer's Guide (docs.libcamera.org). Uses the
// VideoRecording stream role, maps completed frame buffers, and converts
// NV12 / YUV420 / MJPEG output to BGR Mats. Requires the libcamera-dev
// package. libcamera types are hidden behind a pimpl, defined in picamera.cpp.
class PiCamera : public Camera {
    public:
        PiCamera();
        ~PiCamera() override;
        cv::Mat captureImage() override;
        std::string name() const override { return "pi"; }
    private:
        class Impl;
        std::unique_ptr<Impl> impl;
};

#endif
