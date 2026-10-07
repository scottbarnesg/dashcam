#include "video.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

VideoWriter::VideoWriter(std::filesystem::path fileDir) : outputDir(std::move(fileDir)) {
    std::filesystem::create_directories(outputDir);
    writeThread = std::thread(&VideoWriter::writeToFile, this);
}

VideoWriter::~VideoWriter() {
    frameBuffer.close();
    if (writeThread.joinable()) {
        writeThread.join();
    }
    if (dropped_.load() > 0) {
        std::cerr << "VideoWriter dropped " << dropped_.load() << " frames (encoder fell behind)" << std::endl;
    }
}

void VideoWriter::addFrame(cv::Mat frame) {
    if (frame.empty()) {
        return;
    }
    std::scoped_lock lock(frameBufferMut);
    if (frameBuffer.empty()) {
        firstFrameTime = getCurrentTime();
    }
    lastFrameTime = getCurrentTime();
    framesSeen++;
    if (!frameBuffer.push(std::move(frame))) {
        dropped_++;
    }
}

std::chrono::time_point<std::chrono::system_clock> VideoWriter::getCurrentTime() {
    return std::chrono::system_clock::now();
}

std::filesystem::path VideoWriter::generateFilePath() {
    time_t t = std::chrono::system_clock::to_time_t(firstFrameTime);
    auto tm = localtime(&t);
    std::ostringstream timestamp;
    timestamp << std::put_time(tm, "%Y-%m-%d_%H_%M_%S");
    std::filesystem::path fileName = timestamp.str() + ".mp4";
    return outputDir / fileName;
}

int VideoWriter::calculateFPS() {
    std::chrono::duration<float> elapsed = lastFrameTime - firstFrameTime;
    int fps = 1;
    if (elapsed > std::chrono::milliseconds(100) && framesSeen >= 2) {
        fps = (framesSeen - 1) / elapsed.count();
    }
    return std::clamp(fps, 1, 120);
}

void VideoWriter::writeToFile() {
    // Warm up: hold a few frames so the FPS estimate is meaningful before opening the file.
    std::vector<cv::Mat> warmup;
    while (warmup.size() < 10) {
        cv::Mat frame = frameBuffer.pop();
        if (frame.empty()) {
            return; // Buffer closed before any video was produced.
        }
        warmup.push_back(std::move(frame));
    }
    int fps = calculateFPS();
    std::cout << "FPS: " << fps << std::endl;
    std::filesystem::path outputFile = generateFilePath();
    int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    cv::Size frameSize = warmup.front().size();
    cv::VideoWriter writer = cv::VideoWriter(outputFile, fourcc, fps, frameSize);
    if (!writer.isOpened()) {
        std::cerr << "VideoWriter: could not open output file " << outputFile << std::endl;
        return;
    }
    for (cv::Mat& frame : warmup) {
        writer.write(frame);
    }
    while (true) {
        cv::Mat frame = frameBuffer.pop();
        if (frame.empty()) {
            break; // Buffer closed and drained.
        }
        writer.write(frame);
    }
    writer.release();
}
