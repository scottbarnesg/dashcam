#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>

#include <opencv2/core.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/videoio.hpp>

#include "manifest.hpp"
#include "queue.hpp"

#ifndef VIDEO_H
#define VIDEO_H

// Records frames to MP4 files on a background thread, split into fixed-length
// self-closing segments (BACKLOG item 3) so a hard power cut loses at most the
// in-progress segment tail:
//   - each segment is registered in the manifest (item 5a) only once closed;
//   - an open segment carries a "<name>.writing" sentinel file that is removed
//     on clean close; lingering sentinels found at boot mark corrupt files.
// The frame buffer is bounded: if encoding falls behind capture, new frames
// are dropped and counted rather than consuming unbounded memory. The
// destructor closes the buffer (waking the writer thread), flushes, closes the
// current segment, and joins.
class VideoWriter {
    public:
        struct SegmentOptions {
            int lengthSeconds = 120;       // Close and roll to a new file every N seconds.
            int recordingFps = 0;          // Authoritative playback rate; 0 = estimate from frame timing.
            std::string context = "seg";   // Naming context (see naming.hpp).
            std::uintmax_t minFreeBytes = 100ULL * 1024 * 1024; // Pause instead of filling the card.
            Manifest* manifest = nullptr;  // Closed segments registered here (optional).
        };

        VideoWriter(std::filesystem::path fileDir, SegmentOptions options);
        ~VideoWriter();
        void addFrame(cv::Mat frame);
        int droppedFrames() const { return droppedFrameCount.load(); }
        int segmentsWritten() const { return _segmentsWritten; }

    private:
        static constexpr std::size_t kBufferCapacity = 64;
        std::chrono::time_point<std::chrono::system_clock> getCurrentTime();
        int calculateFPS();
        bool hasFreeSpace();
        void openSegment(std::chrono::system_clock::time_point frameTime, cv::Size frameSize);
        void closeSegment();
        void writeToFile();
        std::filesystem::path outputDir;
        SegmentOptions options;
        std::chrono::time_point<std::chrono::system_clock> firstFrameTime;
        std::chrono::time_point<std::chrono::system_clock> lastFrameTime;
        std::atomic<int> framesSeen{0};
        SafeQueue<cv::Mat> frameBuffer{kBufferCapacity};
        std::mutex frameBufferMut;
        std::thread writeThread;
        std::atomic<int> droppedFrameCount{0};
        int _segmentsWritten = 0;

        // Writer-thread-only state:
        cv::VideoWriter segmentWriter;
        std::filesystem::path segmentPath;
        std::filesystem::path sentinelPath;
        std::chrono::system_clock::time_point segmentStart{};
        int segmentFrames = 0;
        int segmentSequence() const;
};

#endif
