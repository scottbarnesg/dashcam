#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include <opencv2/core.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/videoio.hpp>

#include "frame.hpp"
#include "h264encoder.hpp"
#include "manifest.hpp"
#include "mp4muxer.hpp"
#include "queue.hpp"

#ifndef VIDEO_H
#define VIDEO_H

// Records frames to MP4 files on a background thread, split into fixed-length
// self-closing segments (BACKLOG item 3) so a hard power cut loses at most the
// in-progress segment tail:
//   - each segment is registered in the manifest (item 5a) only once closed;
//   - an open segment carries a "<name>.writing" sentinel file that is removed
//     on clean close; lingering sentinels found at boot mark corrupt files.
// Encoding is the Pi's hardware H.264 codec (H264Encoder + fragmented-MP4
// Mp4Muxer) whenever available — hardware segments also stay playable after a
// power cut because fragments close on keyframes. "encoder = sw" (or a codec
// that cannot be opened) falls back to the software cv::VideoWriter path.
// The frame buffer is bounded: if encoding falls behind capture, new frames
// are dropped and counted rather than consuming unbounded memory. Its slots
// hold SharedFrame leases on the camera's pooled buffers (frame_pool.hpp),
// so a full buffer pins a bounded amount of memory instead of allocating
// per frame. The destructor closes the buffer (waking the writer thread),
// flushes, closes the current segment, and joins.
class VideoWriter {
    public:
        struct SegmentOptions {
            int lengthSeconds = 120;       // Close and roll to a new file every N seconds.
            int recordingFps = 0;          // Authoritative playback rate; 0 = estimate from frame timing.
            std::string context = "seg";   // Naming context (see naming.hpp).
            std::uintmax_t minFreeBytes = 100ULL * 1024 * 1024; // Pause instead of filling the card.
            Manifest* manifest = nullptr;  // Closed segments registered here (optional).
            std::string encoder = "auto";  // auto | hw | sw (hw = V4L2 M2M H.264 codec).
            int bitrateKbps = 5000;        // Hardware encoder target bitrate.
            int gopSeconds = 2;            // Hardware encoder IDR interval (fragment length).
            // Invoked on the writer thread when free space is below minFreeBytes
            // just before a segment opens, giving the storage manager (item 5b) a
            // chance to reclaim space so recording can continue. Returns true if
            // anything was freed; the writer re-checks free space either way.
            std::function<bool()> onOutOfSpace = nullptr;
        };

        VideoWriter(std::filesystem::path fileDir, SegmentOptions options);
        ~VideoWriter();
        // Lease-taking hot path: the buffer holds shared_ptr references into
        // the camera's pooled buffers, so enqueueing costs a refcount bump.
        void addFrame(SharedFrame frame);
        // Convenience for BGR callers (tests, dev paths); wraps a copy into
        // an unpooled frame (these callers own their buffers).
        void addFrame(RawFrame frame) { addFrame(std::make_shared<const RawFrame>(std::move(frame))); }
        void addFrame(cv::Mat frame) { addFrame(RawFrame{std::move(frame), PixelFormat::BGR}); }
        int droppedFrames() const { return droppedFrameCount.load(); }
        int segmentsWritten() const { return _segmentsWritten; }

    private:
        // 32 frames ≈ 2 s of slack at the measured 15 fps encode rate behind
        // the hardware encoder: enough to bridge a muxer/segment-close
        // hiccup, small enough that a wedged encoder bounds residency.
        static constexpr std::size_t kBufferCapacity = 32;
        std::chrono::time_point<std::chrono::system_clock> getCurrentTime();
        int calculateFPS();
        bool hasFreeSpace();
        void openSegment(std::chrono::system_clock::time_point frameTime, const RawFrame& firstFrame);
        void closeSegment();
        void submitFrame(const RawFrame& frame);
        void writeToFile();
        std::filesystem::path outputDir;
        SegmentOptions options;
        std::chrono::time_point<std::chrono::system_clock> firstFrameTime;
        std::chrono::time_point<std::chrono::system_clock> lastFrameTime;
        std::atomic<int> framesSeen{0};
        SafeQueue<SharedFrame> frameBuffer{kBufferCapacity};
        std::mutex frameBufferMut;
        std::thread writeThread;
        std::atomic<int> droppedFrameCount{0};
        int _segmentsWritten = 0;

        // Writer-thread-only state:
        cv::VideoWriter segmentWriter;                     // Software fallback path.
        std::unique_ptr<H264Encoder> encoder;              // Hardware path.
        std::unique_ptr<Mp4Muxer> muxer;                   // Hardware path.
        bool usingHw = false;
        bool hwUnavailable = false;                        // "auto": stop retrying after a failure.
        std::filesystem::path segmentPath;
        std::filesystem::path sentinelPath;
        std::chrono::system_clock::time_point segmentStart{};
        int segmentFrames = 0;
        int segmentSequence() const;
};

#endif
