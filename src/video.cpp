#include "video.hpp"

VideoWriter::VideoWriter(std::filesystem::path fileDir) {
    outputDir = fileDir;
    std::filesystem::create_directory(outputDir);
}

void VideoWriter::addFrame(cv::Mat frame) {
    std::scoped_lock lock(frameBufferMut);
    if (frameBuffer.empty()) {
        firstFrameTime = getCurrentTime();
    }
    frameBuffer.push_back(frame);
}

void VideoWriter::writeToFile() {
    std::scoped_lock lock(frameBufferMut);
    if (frameBuffer.empty()) {
        throw std::runtime_error("No frames to write to file");
    }
    // Calculate FPS
    int fps = calculateFPS();
    // Generate filename
    std::filesystem::path outputFile = generateFilePath();
    // Set up VideoWriter
    int fourcc = cv::VideoWriter::fourcc('M', 'P', '4', 'V');
    cv::Size frameSize = frameBuffer[0].size();
    cv::VideoWriter writer = cv::VideoWriter(outputFile, fourcc, fps, frameSize);
    // Write frames to file
    for (cv::Mat frame : frameBuffer) {
        writer.write(frame);
    }
    writer.release();
}

std::chrono::time_point<std::chrono::system_clock> VideoWriter::getCurrentTime() {
    using namespace std::chrono;
    return zoned_time{current_zone(), system_clock::now()};
}

std::filesystem::path VideoWriter::generateFilePath() {
    std::filesystem::path fileName = std::format("{:%Y-%m-%d%H:%M:%S}", firstFrameTime) + ".mp4";
    return outputDir / fileName;
}

int VideoWriter::calculateFPS() {
    std::chrono::duration<float> elapsed = getCurrentTime() - firstFrameTime;
    return float(frameBuffer.size()) / elapsed.count();
}