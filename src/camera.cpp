#include "camera.hpp"

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

std::vector<uchar> USBCamera::encodeImage(cv::Mat image) {
    std::vector<uchar> result;
    cv::imencode(".jpeg", image, result, encodeParams);
    return result;
}