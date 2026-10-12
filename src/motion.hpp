#include <chrono>
#include <mutex>
#include <iostream>

#include <opencv2/core.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/imgproc.hpp>

#include "frame.hpp"

#ifndef MOTION_H
#define MOTION_H

// Frame-differencing motion detector. The comparison runs once per addFrame();
// motionDetected() reports the result for the most recently added frame.
// YUV frames are compared on their Y plane (no color conversion); BGR
// frames are converted to grayscale first. Frames arrive as pooled leases:
// the detector holds the previous and current leases (their buffers stay
// pinned) and keeps the comparison buffers as members, so the steady state
// performs no allocations.
class MotionDetector {
    public:
        explicit MotionDetector(int threshold = 10000) : motionThreshold(threshold) {}
        void addFrame(SharedFrame frame);
        // Convenience for BGR callers (tests, dev paths).
        void addFrame(RawFrame frame) { addFrame(std::make_shared<const RawFrame>(std::move(frame))); }
        void addFrame(cv::Mat frame) { addFrame(RawFrame{std::move(frame), PixelFormat::BGR}); }
        bool motionDetected();
        std::chrono::system_clock::time_point motionLastDetected();
        void reset();
    private:
        bool computeMotion(); // Caller must hold frameMutex.
        // Leases pin the buffers previousFrame/currentFrame view into; the
        // buffers outlive the views by construction.
        SharedFrame previousFrameLease;
        SharedFrame currentFrameLease;
        cv::Mat previousFrame;
        cv::Mat currentFrame;
        // Double-buffered BGR->gray scratch: previousFrame may still view
        // the last conversion's result when the next conversion runs, so a
        // single buffer would diff a frame against itself. The buffers are
        // reused across frames (no steady-state allocation); alternating
        // writes always leave the previous view intact.
        cv::Mat grayscale[2];
        int grayIndex = 0;
        cv::Mat frameDiff; // Comparison scratch (buffer reused across frames).
        std::mutex frameMutex;
        int motionThreshold = 10000;
        bool lastResult = false;
        std::chrono::system_clock::time_point _motionLastDetected;
};

#endif
