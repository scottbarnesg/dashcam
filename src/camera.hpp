#include <cstddef>
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
        // May block until a frame arrives — stop() unblocks it (the queue is
        // closed, so subsequent calls return empty immediately).
        virtual RawFrame captureImage() = 0;
        // Idempotent shutdown hint for blocking backends: safe to call from a
        // different thread than captureImage. Default: no-op.
        virtual void stop() {}
        // Frames the backend discarded because its internal queue was full.
        // Expected while the pipeline samples below sensor rate (idle); only
        // a concern during recording. Default 0 for non-queueing backends.
        virtual std::size_t queueDrops() const { return 0; }
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
        // Idempotent: stops streaming and closes the frame queue so a
        // concurrent captureImage() returns empty.
        void stop() override;
        // Frames dropped by the libcamera->pipeline queue overflow (counted
        // by SafeQueue; expected at idle pacing, actionable during recording).
        std::size_t queueDrops() const override;
        std::string name() const override { return "pi"; }
    private:
        class Impl;
        std::unique_ptr<Impl> impl;
};

#endif
