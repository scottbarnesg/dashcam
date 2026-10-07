#include "motion.hpp"

#include <gtest/gtest.h>

namespace {
cv::Mat frameWithSquare(int x) {
    cv::Mat frame(240, 320, CV_8UC3, cv::Scalar(30, 30, 30));
    cv::rectangle(frame, cv::Point(x, 60), cv::Point(x + 120, 180), cv::Scalar(255, 255, 255), cv::FILLED);
    return frame;
}
}

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

TEST(MotionDetector, ResetForgetsHistory) {
    MotionDetector detector;
    detector.addFrame(frameWithSquare(20));
    detector.addFrame(frameWithSquare(220));
    ASSERT_TRUE(detector.motionDetected());
    detector.reset();
    EXPECT_FALSE(detector.motionDetected());
}
