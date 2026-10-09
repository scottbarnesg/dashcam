#include <memory>
#include <string>

#include "frame.hpp"

#ifndef CAMERA_H
#define CAMERA_H

// Abstract capture interface. Production backend: PiCamera (libcamera).
// Tests use SimulatedCamera (tests/simulated_camera.hpp).
class Camera {
    public:
        virtual ~Camera() = default;
        // Returns the next frame in the backend's native format (PiCamera:
        // NV12). Empty frame on failure; callers must check frame.empty().
        virtual RawFrame captureImage() = 0;
        virtual std::string name() const = 0;
};

// Factory: "pi" (libcamera).
std::unique_ptr<Camera> createCamera(const std::string& backend);

// Captures from the Pi Camera Module using libcamera directly, following the
// libcamera Application Writer's Guide (docs.libcamera.org). Uses the
// VideoRecording stream role, maps completed frame buffers, and emits
// native-format RawFrames (NV12/I420; MJPEG is decoded to BGR). Requires the
// libcamera-dev package. libcamera types are hidden behind a pimpl, defined
// in picamera.cpp.
class PiCamera : public Camera {
    public:
        PiCamera();
        ~PiCamera() override;
        RawFrame captureImage() override;
        std::string name() const override { return "pi"; }
    private:
        class Impl;
        std::unique_ptr<Impl> impl;
};

#endif
