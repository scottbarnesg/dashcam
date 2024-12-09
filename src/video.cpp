#include "video.hpp"

VideoWriter::VideoWriter(std::filesystem::path fileDir) {
    outputDir = fileDir;
    std::filesystem::create_directory(outputDir);
    writeThread = std::thread(&VideoWriter::writeToFile, this);
}

VideoWriter::~VideoWriter() {
    shutdown = true;
    if (writeThread.joinable()) {
        writeThread.join();
    }
}

void VideoWriter::addFrame(cv::Mat frame) {
    std::scoped_lock lock(frameBufferMut);
    if (frameBuffer.empty()) {
        firstFrameTime = getCurrentTime();
    }
    frameBuffer.push(frame);
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
    std::chrono::duration<float> elapsed = getCurrentTime() - firstFrameTime;
    return float(frameBuffer.size()) / elapsed.count();
}

void VideoWriter::writeToFile() {
    // Wait for first N frame to be added to queue, then calculate FPS and create VideoWriter
    while (frameBuffer.size() < 10) {
        frameBuffer.waitForNewItem();
    }
     // Calculate FPS
    int fps = calculateFPS();
    std::cout << "FPS: " << fps << std::endl; 
    // Generate filename
    std::filesystem::path outputFile = generateFilePath();
    // Set up VideoWriter
    int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    cv::Size frameSize = frameBuffer.front().size();
    cv::VideoWriter writer = cv::VideoWriter(outputFile, fourcc, fps, frameSize);
    while (!shutdown) {
        // Wait for new frame to be added to buffer
        cv::Mat frame = frameBuffer.pop();
        // Write this frame to the file
        writer.write(frame);
    }
    // Flush the buffer
    while (!frameBuffer.empty()) {
        // Wait for new frame to be added to buffer
        cv::Mat frame = frameBuffer.pop();
        // Write this frame to the file
        writer.write(frame);
    }
    // Release the video writer
    writer.release();
}