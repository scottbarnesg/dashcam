#include "motion.hpp"

void MotionDetector::addFrame(RawFrame frame) {
    if (frame.empty()) {
        return;
    }
    std::scoped_lock lock(frameMutex);
    previousFrame = currentFrame.clone(); // Current frame becomes previous frame
    // New frame becomes current frame, reduced to grayscale / luma.
    switch (frame.format) {
        case PixelFormat::NV12:
        case PixelFormat::I420:
            // Y plane is the first H rows of the packed YUV mat; a header-only
            // view, no copy.
            currentFrame = frame.data.rowRange(0, frame.size().height);
            break;
        case PixelFormat::BGR:
            cv::cvtColor(frame.data, currentFrame, cv::COLOR_BGR2GRAY);
            break;
    }
    lastResult = computeMotion();
}

// Caller must hold frameMutex.
bool MotionDetector::computeMotion() {
    // Verify we actually have 2 frames
    if (currentFrame.empty() || previousFrame.empty()) {
        return false;
    }
    // Perform background subtraction
    cv::Mat frameDiff;
    cv::absdiff(previousFrame, currentFrame, frameDiff);
    // Calculate the threshold
    cv::threshold(frameDiff, frameDiff, 25, 255, cv::THRESH_BINARY);
    // TODO: Dilate threshold
    std::vector<std::vector<cv::Point> > contours;
    cv::findContours(frameDiff, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    for (std::vector<cv::Point> contour : contours) {
        if (cv::contourArea(contour) >= motionThreshold) {
            _motionLastDetected = std::chrono::system_clock::now();
            return true;
        }
    }
    return false;
}

bool MotionDetector::motionDetected() {
    std::scoped_lock lock(frameMutex);
    return lastResult;
}

std::chrono::system_clock::time_point MotionDetector::motionLastDetected() {
    return _motionLastDetected;
}

void MotionDetector::reset() {
    std::scoped_lock lock(frameMutex);
    previousFrame = cv::Mat{};
    currentFrame = cv::Mat{};
    lastResult = false;
}
