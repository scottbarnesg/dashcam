#include "motion.hpp"
#include "test.hpp"

cv::Mat frameWithSquare(int x) {
    cv::Mat frame(240, 320, CV_8UC3, cv::Scalar(30, 30, 30));
    cv::rectangle(frame, cv::Point(x, 60), cv::Point(x + 120, 180), cv::Scalar(255, 255, 255), cv::FILLED);
    return frame;
}

int main() {
    MotionDetector detector;

    // Empty frames are ignored; no motion with < 2 frames.
    detector.addFrame(cv::Mat());
    CHECK(!detector.motionDetected());

    detector.addFrame(frameWithSquare(20));
    CHECK(!detector.motionDetected());

    // Static scene: no motion.
    detector.addFrame(frameWithSquare(20));
    CHECK(!detector.motionDetected());

    // Object jumps across the frame: motion.
    detector.addFrame(frameWithSquare(220));
    CHECK(detector.motionDetected());

    // Back to static: motion stops after diff falls below threshold.
    detector.addFrame(frameWithSquare(220));
    CHECK(!detector.motionDetected());

    // reset() forgets history.
    detector.addFrame(frameWithSquare(20));
    detector.reset();
    CHECK(!detector.motionDetected());

    TEST_RESULT();
}
