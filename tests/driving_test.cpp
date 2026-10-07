#include "driving.hpp"

#include <gtest/gtest.h>

using Clock = std::chrono::system_clock;
using State = DrivingController::State;

namespace {
DrivingController::Params fastParams(std::chrono::seconds timeout) {
    DrivingController::Params params;
    params.noMotionTimeout = timeout;
    params.idleFps = 1000;   // don't let pacing interfere with state tests
    params.recordingFps = 1000;
    return params;
}
}

TEST(DrivingController, StartsRecordingOnFirstMotion) {
    DrivingController c(fastParams(std::chrono::seconds(2)));
    auto t = Clock::now();
    EXPECT_FALSE(c.onFrame(t, false));
    EXPECT_EQ(c.state(), State::Idle);

    t += std::chrono::milliseconds(200);
    EXPECT_TRUE(c.onFrame(t, true));
    EXPECT_EQ(c.state(), State::Recording);
}

TEST(DrivingController, MotionWithinTimeoutKeepsRecording) {
    DrivingController c(fastParams(std::chrono::seconds(2)));
    auto t = Clock::now();
    c.onFrame(t, true);
    for (int i = 0; i < 4; i++) {
        t += std::chrono::milliseconds(500);
        EXPECT_TRUE(c.onFrame(t, i % 2 == 0)); // motion every 1s, timeout is 2s
    }
    EXPECT_EQ(c.state(), State::Recording);
}

TEST(DrivingController, StopsAfterTimeoutAndRestarts) {
    DrivingController c(fastParams(std::chrono::seconds(2)));
    auto t = Clock::now();
    c.onFrame(t, true);                       // motion at t
    t += std::chrono::milliseconds(1000);
    EXPECT_TRUE(c.onFrame(t, false));         // 1.0s without motion: still recording
    t += std::chrono::milliseconds(1200);     // 2.2s without motion
    EXPECT_FALSE(c.onFrame(t, false));
    EXPECT_EQ(c.state(), State::Idle);

    t += std::chrono::milliseconds(100);
    EXPECT_TRUE(c.onFrame(t, true));          // new motion restarts recording
    EXPECT_EQ(c.state(), State::Recording);
}

TEST(DrivingController, PacingMatchesState) {
    DrivingController::Params p;
    p.recordingFps = 20;                      // 50ms period
    p.idleFps = 2;                            // 500ms period
    DrivingController c(p);
    auto t = Clock::now();
    c.onFrame(t, false);
    EXPECT_GE(c.timeUntilNextCapture(t), std::chrono::milliseconds(450));
    c.onFrame(t, true);
    auto wait = c.timeUntilNextCapture(t);
    EXPECT_LE(wait, std::chrono::milliseconds(60));
    EXPECT_GE(wait, std::chrono::milliseconds(40));
}

TEST(DrivingController, PacingNeverNegative) {
    DrivingController c(DrivingController::Params{});
    auto t = Clock::now();
    c.onFrame(t, false);
    EXPECT_EQ(c.timeUntilNextCapture(t + std::chrono::hours(1)), std::chrono::microseconds::zero());
}
