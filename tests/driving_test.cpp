#include "driving.hpp"
#include "test.hpp"

using Clock = std::chrono::system_clock;

// 200ms idle, one motion frame -> recording; motion every 1s keeps it alive;
// 5s of no motion with N=2s -> stops. Also checks capture pacing.
int main() {
    DrivingController::Params params;
    params.noMotionTimeout = std::chrono::seconds(2);
    params.idleFps = 1000;      // don't let pacing interfere with tests
    params.recordingFps = 1000;
    DrivingController c(params);

    auto t = Clock::now();
    // Idle frames don't record.
    CHECK(!c.onFrame(t, false));
    CHECK(c.state() == DrivingController::State::Idle);

    // First motion starts recording immediately.
    t += std::chrono::milliseconds(200);
    CHECK(c.onFrame(t, true));
    CHECK(c.state() == DrivingController::State::Recording);

    // Motion just under the timeout keeps recording.
    for (int i = 0; i < 4; i++) {
        t += std::chrono::milliseconds(500); // 0.5s intervals, timeout is 2s
        CHECK(c.onFrame(t, i % 2 == 0 ? true : false));
    }
    CHECK(c.state() == DrivingController::State::Recording);

    // No motion for just under N (last motion was 1.0s ago): still recording.
    t += std::chrono::milliseconds(500);
    CHECK(c.onFrame(t, false));
    CHECK(c.state() == DrivingController::State::Recording);

    // Past N (last motion now ~2.1s ago): stop.
    t += std::chrono::milliseconds(1200);
    CHECK(!c.onFrame(t, false));
    CHECK(c.state() == DrivingController::State::Idle);

    // Can restart on new motion.
    t += std::chrono::milliseconds(100);
    CHECK(c.onFrame(t, true));
    CHECK(c.state() == DrivingController::State::Recording);

    // Pacing: with recordingFps=20, next deadline ~50ms out.
    DrivingController::Params p2;
    p2.recordingFps = 20;
    p2.idleFps = 2;
    DrivingController pacer(p2);
    auto t2 = Clock::now();
    pacer.onFrame(t2, false);
    auto wait = pacer.timeUntilNextCapture(t2);
    CHECK(wait >= std::chrono::milliseconds(450)); // idle: 1/2 s
    pacer.onFrame(t2, true);
    wait = pacer.timeUntilNextCapture(t2);
    CHECK(wait <= std::chrono::milliseconds(60)); // recording: 1/20 s
    CHECK(wait >= std::chrono::milliseconds(40));

    // Never negative in the past.
    CHECK(pacer.timeUntilNextCapture(t2 + std::chrono::hours(1)) == std::chrono::microseconds::zero());

    TEST_RESULT();
}
