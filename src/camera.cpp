#include "camera_impl.hpp"

#include <chrono>
#include <cstdlib>
#include <thread>

#include <opencv2/imgproc.hpp>

USBCamera::USBCamera() {
    cam = cv::VideoCapture(cameraIndex);
    cam.set(cv::CAP_PROP_BUFFERSIZE, 1);
    if (!cam.isOpened()) {
        throw std::runtime_error("Could not open webcam");
    }
}

USBCamera::~USBCamera() {
    cam.release();
}

cv::Mat USBCamera::captureImage() {
    cv::Mat image;
    cam >> image;
    return image;
}

// SimulatedCamera has moved to tests/simulated_camera.hpp (test-only).

std::unique_ptr<Camera> createCamera(const std::string& backend) {
    if (backend == "usb") {
        return std::make_unique<USBCamera>();
    }
#ifdef USE_PI_CAMERA
    if (backend == "pi") {
        return std::make_unique<PiCamera>();
    }
#endif
    throw std::runtime_error("Unknown camera backend: " + backend);
}
