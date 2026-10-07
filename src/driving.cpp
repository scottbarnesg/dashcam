#include "driving.hpp"

#include <algorithm>

DrivingController::DrivingController(Params params) : params_(params) {
}

double DrivingController::fps() const {
    return state_ == State::Idle ? params_.idleFps : params_.recordingFps;
}

bool DrivingController::onFrame(std::chrono::system_clock::time_point now, bool motion) {
    if (motion) {
        lastMotion_ = now;
        if (state_ == State::Idle) {
            state_ = State::Recording;
        }
    } else if (state_ == State::Recording && (now - lastMotion_) >= params_.noMotionTimeout) {
        state_ = State::Idle;
    }
    nextCaptureDeadline_ = now + std::chrono::microseconds(static_cast<long long>(1e6 / fps()));
    return state_ == State::Recording;
}

std::chrono::microseconds DrivingController::timeUntilNextCapture(std::chrono::system_clock::time_point now) const {
    auto remaining = nextCaptureDeadline_ - now;
    if (remaining < std::chrono::microseconds::zero()) {
        remaining = std::chrono::microseconds::zero();
    }
    return std::chrono::duration_cast<std::chrono::microseconds>(remaining);
}
