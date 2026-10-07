#include <chrono>
#include <mutex>
#include <iostream>

#include <opencv2/core.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgproc.hpp>

#ifndef MOTION_H
#define MOTION_H

// Frame-differencing motion detector. The comparison runs once per addFrame();
// motionDetected() reports the result for the most recently added frame.
class MotionDetector {
    public:
        explicit MotionDetector(int threshold = 10000) : motionThreshold(threshold) {}
        void addFrame(cv::Mat frame);
        bool motionDetected();
        std::chrono::system_clock::time_point motionLastDetected();
        void reset();
    private:
        bool computeMotion(); // Caller must hold frameMutex.
        cv::Mat previousFrame;
        cv::Mat currentFrame;
        std::mutex frameMutex;
        int motionThreshold = 10000;
        bool lastResult = false;
        std::chrono::system_clock::time_point _motionLastDetected;
};

#endif
