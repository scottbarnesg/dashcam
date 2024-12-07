#include <iostream>

#include "camera.hpp"
#include "motion.hpp"
#include "video.hpp"

int main() {
    USBCamera camera = USBCamera();
    /*
    MotionDetector motionDetector = MotionDetector();
    while (true) {
        auto frame = camera.captureImage();
        motionDetector.addFrame(frame);
        if (motionDetector.motionDetected()) {
            std::cout << "Motion detected!" << std::endl;
        }
        else {
            std::cout << "No motion detected" << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    */
    
    
    std::chrono::seconds recordingDuration(10);
    
    VideoWriter writer = VideoWriter("videos/");
    std::chrono::time_point<std::chrono::system_clock> startTime = std::chrono::system_clock::now();
    while ((std::chrono::system_clock::now() - startTime) < recordingDuration) {
        auto frame = camera.captureImage();
        writer.addFrame(frame);
    }
    
    return 0;
}