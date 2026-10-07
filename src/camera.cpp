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

SimulatedCamera::SimulatedCamera(int width, int height) : frameWidth(width), frameHeight(height) {
}

cv::Mat SimulatedCamera::captureImage() {
    cv::Mat frame(frameHeight, frameWidth, CV_8UC3, cv::Scalar(30, 60, 90));
    if (_moving) {
        int x = (frameNumber * 37) % (frameWidth - 100);
        int y = (frameNumber * 53) % (frameHeight - 100);
        cv::rectangle(frame, cv::Point(x, y), cv::Point(x + 90, y + 90), cv::Scalar(255, 255, 255), cv::FILLED);
    } else {
        cv::rectangle(frame, cv::Point(50, 50), cv::Point(140, 140), cv::Scalar(200, 200, 200), cv::FILLED);
    }
    frameNumber++;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return frame;
}

std::unique_ptr<Camera> createCamera(const std::string& backend) {
    if (backend == "usb") {
        return std::make_unique<USBCamera>();
    }
    if (backend == "sim") {
        return std::make_unique<SimulatedCamera>();
    }
#ifdef USE_PI_CAMERA
    if (backend == "pi") {
        return std::make_unique<PiCamera>();
    }
#endif
    throw std::runtime_error("Unknown camera backend: " + backend);
}
