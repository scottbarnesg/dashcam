#include <memory>
#include <string>

#include <opencv2/core.hpp>

#ifndef CAMERA_H
#define CAMERA_H

// Abstract capture interface. Backends: USBCamera (dev/test), PiCamera (rpicam-vid, Pi only),
// SimulatedCamera (synthetic frames for tests).
class Camera {
    public:
        virtual ~Camera() = default;
        // Returns the next frame. Empty Mat on failure; callers must check frame.empty().
        virtual cv::Mat captureImage() = 0;
        virtual std::string name() const = 0;
};

// Factory: "usb" | "sim" | "pi" ("pi" only available when built with -DUSE_PI_CAMERA=ON).
std::unique_ptr<Camera> createCamera(const std::string& backend);

#endif
