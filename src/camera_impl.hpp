#include "camera.hpp"

#include <stdexcept>

#include <opencv2/videoio.hpp>

#ifndef CAMERA_IMPL_H
#define CAMERA_IMPL_H

class USBCamera : public Camera {
    public:
        USBCamera();
        ~USBCamera() override;
        cv::Mat captureImage() override;
        std::string name() const override { return "usb"; }
    private:
        cv::VideoCapture cam;
        int cameraIndex = 0;
};

// Synthetic camera for tests: a background with a rectangle that jumps around while "moving".
class SimulatedCamera : public Camera {
    public:
        explicit SimulatedCamera(int width = 640, int height = 480);
        cv::Mat captureImage() override;
        std::string name() const override { return "sim"; }
        void setMoving(bool moving) { moving_ = moving; }
    private:
        int width_;
        int height_;
        bool moving_ = false;
        int frameNumber_ = 0;
};

#ifdef USE_PI_CAMERA
// Captures from the Pi Camera Module by piping the MJPEG byte stream produced by
// "rpicam-vid --codec mjpeg -o -" into memory and decoding each JPEG frame.
// See BACKLOG item 1; implemented per Raspberry Pi camera software docs.
class PiCamera : public Camera {
    public:
        PiCamera();
        ~PiCamera() override;
        cv::Mat captureImage() override;
        std::string name() const override { return "pi"; }
    private:
        bool pump(); // Read from the pipe until at least one full JPEG is buffered.
        FILE* pipe = nullptr;
        std::vector<unsigned char> buffer;
};
#endif

#endif
