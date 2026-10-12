#include "motion.hpp"

void MotionDetector::addFrame(SharedFrame frame) {
    if (!frame || frame->empty()) {
        return;
    }
    std::scoped_lock lock(frameMutex);
    previousFrameLease = std::move(currentFrameLease);
    previousFrame = currentFrame; // Header over the previous lease's buffer.
    switch (frame->format) {
        case PixelFormat::NV12:
        case PixelFormat::I420:
            // Y plane is the first H rows of the packed YUV mat; a header-only
            // view, no copy. The lease keeps the buffer alive while views live.
            currentFrame = frame->data.rowRange(0, frame->size().height);
            break;
        case PixelFormat::BGR:
            // Alternating scratch buffers: a conversion must not overwrite
            // what previousFrame is still viewing.
            grayIndex ^= 1;
            cv::cvtColor(frame->data, grayscale[grayIndex], cv::COLOR_BGR2GRAY);
            currentFrame = grayscale[grayIndex];
            break;
    }
    currentFrameLease = std::move(frame);
    lastResult = computeMotion();
}

// Caller must hold frameMutex.
bool MotionDetector::computeMotion() {
    // Verify we actually have 2 frames
    if (currentFrame.empty() || previousFrame.empty()) {
        return false;
    }
    // Perform background subtraction (member buffer: no per-frame allocation).
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
    previousFrameLease.reset();
    currentFrameLease.reset();
    previousFrame = cv::Mat{};
    currentFrame = cv::Mat{};
    grayscale[0] = cv::Mat{};
    grayscale[1] = cv::Mat{};
    lastResult = false;
}
