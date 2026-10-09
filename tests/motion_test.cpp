#include "motion.hpp"

#include <gtest/gtest.h>

namespace {
cv::Mat frameWithSquare(int x) {
    cv::Mat frame(240, 320, CV_8UC3, cv::Scalar(30, 30, 30));
    cv::rectangle(frame, cv::Point(x, 60), cv::Point(x + 120, 180), cv::Scalar(255, 255, 255), cv::FILLED);
    return frame;
}
// Same scene in the Pi backend's native format: build I420 (OpenCV can
// produce that directly) then interleave the chroma planes into NV12.
RawFrame nv12FrameWithSquare(int x) {
    cv::Mat i420;
    cv::cvtColor(frameWithSquare(x), i420, cv::COLOR_BGR2YUV_I420);
    const int W = 320, H = 240;
    cv::Mat nv12(H * 3 / 2, W, CV_8UC1);
    i420.rowRange(0, H).copyTo(nv12.rowRange(0, H));
    const uint8_t* u = i420.ptr(H);
    const uint8_t* v = u + (W / 2) * (H / 2);
    for (int row = 0; row < H / 2; row++) {
        uint8_t* dst = nv12.ptr(H + row);
        for (int col = 0; col < W / 2; col++) {
            dst[2 * col] = u[row * (W / 2) + col];
            dst[2 * col + 1] = v[row * (W / 2) + col];
        }
    }
    return RawFrame{nv12, PixelFormat::NV12};
}
} // namespace

TEST(MotionDetector, IgnoresEmptyAndSingleFrames) {
    MotionDetector detector;
    detector.addFrame(cv::Mat());
    EXPECT_FALSE(detector.motionDetected());
    detector.addFrame(frameWithSquare(20));
    EXPECT_FALSE(detector.motionDetected());
}

TEST(MotionDetector, StaticSceneIsNoMotion) {
    MotionDetector detector;
    detector.addFrame(frameWithSquare(20));
    detector.addFrame(frameWithSquare(20));
    EXPECT_FALSE(detector.motionDetected());
}

TEST(MotionDetector, DetectsMovingObject) {
    MotionDetector detector;
    detector.addFrame(frameWithSquare(20));
    detector.addFrame(frameWithSquare(220));
    EXPECT_TRUE(detector.motionDetected());

    detector.addFrame(frameWithSquare(220));  // static again
    EXPECT_FALSE(detector.motionDetected());
}

TEST(MotionDetector, Nv12FramesCompareLumaPlane) {
    MotionDetector detector;
    detector.addFrame(nv12FrameWithSquare(20));
    detector.addFrame(nv12FrameWithSquare(20));
    EXPECT_FALSE(detector.motionDetected());
    detector.addFrame(nv12FrameWithSquare(220));
    EXPECT_TRUE(detector.motionDetected());
}

TEST(MotionDetector, ResetForgetsHistory) {
    MotionDetector detector;
    detector.addFrame(frameWithSquare(20));
    detector.addFrame(frameWithSquare(220));
    ASSERT_TRUE(detector.motionDetected());
    detector.reset();
    EXPECT_FALSE(detector.motionDetected());
}
