#include <mutex>
#include <iostream>

#include <opencv2/core.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgproc.hpp>

#ifndef MOTION_H
#define MOTION_H

class MotionDetector {
    public:
        void addFrame(cv::Mat frame);
        bool motionDetected();
    private:
        cv::Mat previousFrame;
        cv::Mat currentFrame;
        std::mutex frameMutex;
        int motionThreshold = 10000;
};

#endif