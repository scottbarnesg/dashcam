#include <iostream>

#include "camera.hpp"
#include "motion.hpp"
#include "video.hpp"

void recordVideo(USBCamera* camera, MotionDetector* detector) {
    std::chrono::seconds recordingDuration(10);
    VideoWriter writer = VideoWriter("videos/");
    std::chrono::time_point<std::chrono::system_clock> startTime = std::chrono::system_clock::now();
    // TODO: Update this loop to keep recording as long as motion is detected
    while ((std::chrono::system_clock::now() - startTime) < recordingDuration) {
        // Capture the frame and add it to the video
        auto frame = camera->captureImage();
        writer.addFrame(frame);
        // Continue to perform motion detection
        detector->addFrame(frame);
        if (detector->motionDetected()) {
            // If motion is detected, reset the start time
            startTime = std::chrono::system_clock::now();
        }
    }
    detector->reset();
}

int main() {
    USBCamera camera = USBCamera();
    MotionDetector motionDetector = MotionDetector();
    std::chrono::milliseconds captureDelay(500); // Interval between frames being captured to check for motion
    while (true) {
        std::chrono::time_point<std::chrono::system_clock> startTime = std::chrono::system_clock::now();
        auto frame = camera.captureImage();
        motionDetector.addFrame(frame);
        if (motionDetector.motionDetected()) {
            std::cout << "Motion detected! Recording video..." << std::endl;
            recordVideo(&camera, &motionDetector);
            std::cout << "Done recording video." << std::endl;
        }
        std::chrono::duration elapsed = std::chrono::system_clock::now() - startTime;
        if (elapsed < captureDelay) {
            std::this_thread::sleep_for(captureDelay - elapsed);
        }
    }
    return 0;
}