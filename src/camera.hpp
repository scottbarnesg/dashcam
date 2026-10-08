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

#endif
