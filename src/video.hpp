#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>

#include <opencv2/core.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/videoio.hpp>

#include "queue.hpp"

#ifndef VIDEO_H
#define VIDEO_H

// Records frames to a timestamped MP4 file on a background thread. The frame
// buffer is bounded: if encoding falls behind capture, new frames are dropped
// and counted rather than consuming unbounded memory. The destructor closes the
// buffer (waking the writer thread), flushes remaining frames, and joins.
class VideoWriter {
    public:
        explicit VideoWriter(std::filesystem::path fileDir);
        ~VideoWriter();
        void addFrame(cv::Mat frame);
        int droppedFrames() const { return dropped_.load(); }
    private:
        static constexpr std::size_t kBufferCapacity = 64;
        std::chrono::time_point<std::chrono::system_clock> getCurrentTime();
        std::filesystem::path generateFilePath();
        int calculateFPS();
        void writeToFile();
        std::filesystem::path outputDir;
        std::chrono::time_point<std::chrono::system_clock> firstFrameTime;
        std::chrono::time_point<std::chrono::system_clock> lastFrameTime;
        std::atomic<int> framesSeen{0};
        SafeQueue<cv::Mat> frameBuffer{kBufferCapacity};
        std::mutex frameBufferMut;
        std::thread writeThread;
        std::atomic<int> dropped_{0};
};

#endif
