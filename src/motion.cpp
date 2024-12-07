#include "motion.hpp"


void MotionDetector::addFrame(cv::Mat frame) {
    std::scoped_lock lock(frameMutex);
    previousFrame = currentFrame.clone(); // Current frame becomes previous frame
    cv::cvtColor(frame, currentFrame, cv::COLOR_BGR2GRAY); // New frame becomes current frame, converted to grayscale
}

bool MotionDetector::motionDetected() {
    std::scoped_lock lock(frameMutex);
    // Verify we actually have 2 frames
    if (currentFrame.empty() || previousFrame.empty()) {
        return false;
    }
    // Perform background subtraction
    cv::Mat frameDiff;
    cv::absdiff(previousFrame, currentFrame, frameDiff);
    // Calculate and dilate the threshold
    cv::Mat threshold;
    cv::threshold(frameDiff, threshold, 25, 255, cv::THRESH_BINARY);
    // TODO: Dilate threshold
    // TODO: Check if any of the contours exceed the motion threshold
    std::vector<std::vector<cv::Point> > contours;
    cv::findContours(threshold.clone(), contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (std::vector<cv::Point> contour : contours) {
        if (cv::contourArea(contour) >= motionThreshold) {
            return true;
        }
    }
    return false;
}

