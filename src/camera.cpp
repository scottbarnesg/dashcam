#include "camera.hpp"

#include <stdexcept>

std::unique_ptr<Camera> createCamera(const std::string& backend) {
    if (backend == "pi") {
        return std::make_unique<PiCamera>();
    }
    throw std::runtime_error("Unknown camera backend: " + backend);
}
