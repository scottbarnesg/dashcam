#include <chrono>
#include <filesystem>
#include <format>
#include <mutex>
#include <vector>

#include <opencv2/videoio.hpp>

class VideoWriter {
    public:
        VideoWriter(std::filesystem::path fileDir);
        void addFrame(cv::Mat frame);
        void writeToFile();
    private:
        std::chrono::time_point<std::chrono::system_clock> getCurrentTime();
        std::filesystem::path generateFilePath();
        int calculateFPS();
        std::filesystem::path outputDir;
        std::chrono::time_point<std::chrono::system_clock> firstFrameTime;
        std::vector<cv::Mat> frameBuffer = {};
        std::mutex frameBufferMut;
};