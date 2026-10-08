#ifndef SIMULATED_CAMERA_HPP
#define SIMULATED_CAMERA_HPP

#include <chrono>
#include <thread>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "camera.hpp"

// Test-only camera: a background with a rectangle that jumps around while
// "moving". Lives in tests/ on purpose - never compiled into the binary.
class SimulatedCamera : public Camera {
    public:
        explicit SimulatedCamera(int width = 640, int height = 480) : frameWidth(width), frameHeight(height) {}
        cv::Mat captureImage() override {
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
        std::string name() const override { return "sim"; }
        void setMoving(bool moving) { _moving = moving; }
    private:
        int frameWidth;
        int frameHeight;
        bool _moving = false;
        int frameNumber = 0;
};

#endif
