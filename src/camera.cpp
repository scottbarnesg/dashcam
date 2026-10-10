#include "camera.hpp"

#include <stdexcept>

std::unique_ptr<Camera> createCamera(const std::string& backend, CameraOrientation orientation) {
    if (backend == "pi") {
        return std::make_unique<PiCamera>(orientation);
    }
    throw std::runtime_error("Unknown camera backend: " + backend);
}
