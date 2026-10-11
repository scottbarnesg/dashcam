#include "driving.hpp"

#include <algorithm>

bool parseOperatingMode(const std::string& text, OperatingMode& out) {
    if (text == "driving") {
        out = OperatingMode::Driving;
        return true;
    }
    if (text == "persistent") {
        out = OperatingMode::Persistent;
        return true;
    }
    return false;
}

const char* operatingModeName(OperatingMode mode) {
    return mode == OperatingMode::Driving ? "driving" : "persistent";
}

DrivingController::DrivingController(Params params) : settings(params) {
}

double DrivingController::fps() const {
    return _state == State::Idle ? settings.idleFps : settings.recordingFps;
}

bool DrivingController::onFrame(std::chrono::system_clock::time_point now, bool motion) {
    if (motion) {
        lastMotionTime = now;
    }
    if (settings.mode == OperatingMode::Driving) {
        // Power presence is the driving signal: recording is never gated on
        // motion. Motion still updates lastMotionTime for future event tagging.
        _state = State::Recording;
    } else if (motion) {
        if (_state == State::Idle) {
            _state = State::Recording;
        }
    } else if (_state == State::Recording && (now - lastMotionTime) >= settings.noMotionTimeout) {
        _state = State::Idle;
    }
    nextCaptureDeadline = now + std::chrono::microseconds(static_cast<long long>(1e6 / fps()));
    return _state == State::Recording;
}

std::chrono::microseconds DrivingController::timeUntilNextCapture(std::chrono::system_clock::time_point now) const {
    auto remaining = nextCaptureDeadline - now;
    if (remaining < std::chrono::microseconds::zero()) {
        remaining = std::chrono::microseconds::zero();
    }
    return std::chrono::duration_cast<std::chrono::microseconds>(remaining);
}
