#include <iostream>

#include "camera.hpp"
#include "video.hpp"

int main() {
    std::chrono::seconds recordingDuration(10);
    USBCamera camera = USBCamera();
    VideoWriter writer = VideoWriter("videos/");
    std::chrono::time_point<std::chrono::system_clock> startTime = std::chrono::system_clock::now();
    while ((std::chrono::system_clock::now() - startTime) < recordingDuration) {
        auto frame = camera.captureImage();
        writer.addFrame(frame);
    }
    return 0;
}