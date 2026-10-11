#include <chrono>
#include <string>

#ifndef DRIVING_HPP
#define DRIVING_HPP

// Operating mode (BACKLOG item 10): an explicit config choice that selects
// both the power model and the recording trigger, superseding the §2
// motion-as-driving proxy for the driving case.
//   Driving:    ignition-powered; record continuously whenever powered —
//               power presence *is* the driving signal; motion never gates
//               recording (it may still run for future event tagging).
//   Persistent: battery-powered; recording is conditioned on motion
//               detection (§2 state machine, parked-surveillance behavior).
enum class OperatingMode { Driving, Persistent };

// Parses "driving" / "persistent"; false (and out untouched) otherwise.
bool parseOperatingMode(const std::string& text, OperatingMode& out);
const char* operatingModeName(OperatingMode mode);

// Motion-as-driving-proxy state machine (BACKLOG item 2), gated by the
// operating mode (item 10). Persistent mode:
// IDLE: no motion seen recently. RECORDING: motion seen within the no-motion timeout N.
// Motion starts recording immediately; recording stops only after N consecutive seconds
// without motion. Mid-drive fragmentation at stop lights is accepted by design.
// Driving mode: always Recording from the first frame.
class DrivingController {
    public:
        struct Params {
            OperatingMode mode = OperatingMode::Persistent;
            std::chrono::seconds noMotionTimeout{120};   // N: stop recording after this long without motion (persistent mode only)
            double idleFps = 2.0;                        // capture rate while idle (persistent mode only)
            double recordingFps = 30.0;                  // capture rate while recording
        };

        enum class State { Idle, Recording };

        explicit DrivingController(Params params);

        // Feed one observation per captured frame. Returns true while a recording is active
        // (the caller must pass frames to the video writer when this returns true).
        bool onFrame(std::chrono::system_clock::time_point now, bool motion);

        State state() const { return _state; }
        OperatingMode mode() const { return settings.mode; }
        std::chrono::microseconds timeUntilNextCapture(std::chrono::system_clock::time_point now) const;

    private:
        Params settings;
        State _state = State::Idle;
        std::chrono::system_clock::time_point lastMotionTime{};
        std::chrono::system_clock::time_point nextCaptureDeadline{};
        double fps() const;
};

#endif
