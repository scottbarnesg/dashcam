#include <atomic>
#include <ctime>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>

#include <opencv2/videoio.hpp>

#include "queue.hpp"

#ifndef VIDEO_H
#define VIDEO_H

class VideoWriter {
    public:
        VideoWriter(std::filesystem::path fileDir);
        ~VideoWriter();
        void addFrame(cv::Mat frame);
    private:
        std::chrono::time_point<std::chrono::system_clock> getCurrentTime();
        std::filesystem::path generateFilePath();
        int calculateFPS();
        void writeToFile();
        std::filesystem::path outputDir;
        std::chrono::time_point<std::chrono::system_clock> firstFrameTime;
        SafeQueue<cv::Mat> frameBuffer;
        std::mutex frameBufferMut;
        std::thread writeThread;
        std::atomic<bool> shutdown = false;
};

#endif