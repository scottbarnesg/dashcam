#include <chrono>
#include <functional>

#ifndef DRIVING_HPP
#define DRIVING_HPP

// Motion-as-driving-proxy state machine (BACKLOG item 2).
// IDLE: no motion seen recently. RECORDING: motion seen within the no-motion timeout N.
// Motion starts recording immediately; recording stops only after N consecutive seconds
// without motion. Mid-drive fragmentation at stop lights is accepted by design.
class DrivingController {
    public:
        struct Params {
            std::chrono::seconds noMotionTimeout{120};   // N: stop recording after this long without motion
            double idleFps = 2.0;                        // capture rate while idle
            double recordingFps = 12.0;                  // capture rate while recording
        };

        enum class State { Idle, Recording };

        explicit DrivingController(Params params);

        // Feed one observation per captured frame. Returns true while a recording is active
        // (the caller must pass frames to the video writer when this returns true).
        bool onFrame(std::chrono::system_clock::time_point now, bool motion);

        State state() const { return _state; }
        std::chrono::microseconds timeUntilNextCapture(std::chrono::system_clock::time_point now) const;

    private:
        Params settings;
        State _state = State::Idle;
        std::chrono::system_clock::time_point lastMotionTime{};
        std::chrono::system_clock::time_point nextCaptureDeadline{};
        double fps() const;
};

#endif
