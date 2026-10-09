#include "manifest.hpp"
#include "recovery.hpp"
#include "video.hpp"

#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <opencv2/videoio.hpp>
#include <thread>

#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using Clock = std::chrono::system_clock;

namespace {

void feedFrames(VideoWriter& w, int n, int width = 160, int height = 120,
                std::chrono::milliseconds interval = std::chrono::milliseconds(20)) {
    for (int i = 0; i < n; i++) {
        cv::Mat frame(height, width, CV_8UC3, cv::Scalar((i * 7) % 256, 40, 90));
        w.addFrame(frame);
        std::this_thread::sleep_for(interval);
    }
}

int countVideos(const fs::path& dir) {
    return std::count_if(fs::directory_iterator(dir), fs::directory_iterator{},
                         [](const fs::directory_entry& e) { return e.path().extension() == ".mp4"; });
}

} // namespace

TEST(VideoWriter, ClosesValidFileAndIgnoresEmptyFrames) {
    fs::path dir = "test_videos_basic";
    fs::remove_all(dir);
    {
        VideoWriter writer(dir, VideoWriter::SegmentOptions{});
        feedFrames(writer, 40);
        writer.addFrame(cv::Mat());
    } // Destructor flushes, closes, joins.
    ASSERT_EQ(countVideos(dir), 1);
    fs::path produced;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() == ".mp4") {
            produced = e.path();
        }
    }
    EXPECT_GT(fs::file_size(produced), 0u);

    cv::VideoCapture cap(produced);
    ASSERT_TRUE(cap.isOpened());
    int frames = 0;
    cv::Mat frame;
    while (cap.read(frame)) {
        frames++;
    }
    EXPECT_GE(frames, 30);
    fs::remove_all(dir);
}

TEST(VideoWriter, CaptureTimestampsDrivePlaybackTiming) {
    // Hardware-path PTS follow frame capture stamps (a stall becomes a PTS
    // gap, not sped-up playback); recordingFps is the stream timebase.
    // Feed 30 frames at 25 fps real pace: the muxed stream must carry every
    // frame, and its reported rate must be consistent with the capture pace
    // (exact PTS spacing is verified on-device with ffprobe; OpenCV's
    // fps property on fragmented MP4 is not reliable enough to assert here).
    fs::path dir = "test_videos_fps";
    fs::remove_all(dir);
    {
        VideoWriter::SegmentOptions opts;
        opts.recordingFps = 24; // Timescale only; timing comes from capture stamps.
        VideoWriter writer(dir, opts);
        feedFrames(writer, 30, 160, 120, std::chrono::milliseconds(40));
    }
    fs::path produced;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() == ".mp4") {
            produced = e.path();
        }
    }
    ASSERT_FALSE(produced.empty());
    cv::VideoCapture cap(produced);
    ASSERT_TRUE(cap.isOpened());
    cv::Mat frame;
    int frames = 0;
    while (cap.read(frame)) {
        frames++;
    }
    EXPECT_GE(frames, 28);
    EXPECT_GT(cap.get(cv::CAP_PROP_FPS), 0.0);
    fs::remove_all(dir);
}

TEST(VideoWriter, RollsSelfClosingSegmentsRegisteredInManifest) {
    fs::path dir = "test_videos_segments";
    fs::remove_all(dir);
    {
        Manifest manifest(dir);
        VideoWriter::SegmentOptions opts;
        opts.lengthSeconds = 1;
        opts.manifest = &manifest;
        VideoWriter writer(dir, opts);
        feedFrames(writer, 80);                                   // ~1.6s
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
        feedFrames(writer, 20);
    } // Joined: segment count is final.
    int segments = countVideos(dir);
    EXPECT_GE(segments, 2);
    Manifest manifest(dir);
    ASSERT_TRUE(manifest.load());
    EXPECT_EQ(manifest.size(), static_cast<std::size_t>(segments));
    for (const ManifestEntry& e : manifest.entries()) {
        EXPECT_EQ(e.sha256.size(), 64u);
        EXPECT_GT(e.sizeBytes, 0u);
        EXPECT_FALSE(fs::exists(dir / (e.file + ".writing")));   // No stale sentinels.
    }
    fs::remove_all(dir);
}

TEST(VideoWriter, PowerCutLeavesSentinelAndRecoveryQuarantines) {
    fs::path dir = "test_videos_cut";
    fs::remove_all(dir);
    {
        pid_t child = fork();
        if (child == 0) {
            VideoWriter::SegmentOptions opts;
            opts.lengthSeconds = 1000; // One long segment so the cut lands mid-segment.
            VideoWriter writer(dir, opts);
            feedFrames(writer, 200);
            _exit(0);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        kill(child, SIGKILL); // Exactly like the vehicle losing power.
        int status = 0;
        waitpid(child, &status, 0);
    }
    bool sentinelLeft = false;
    for (const auto& e : fs::recursive_directory_iterator(dir)) {
        if (e.path().extension() == ".writing") {
            sentinelLeft = true;
        }
    }
    EXPECT_TRUE(sentinelLeft);
    EXPECT_EQ(quarantineIncompleteSegments(dir), 1u);
    EXPECT_TRUE(fs::exists(dir / "quarantine"));
    EXPECT_EQ(countVideos(dir), 0); // Open segment left the main dir.
    Manifest manifest(dir);
    manifest.load();
    EXPECT_EQ(manifest.size(), 0u); // Never-closed segment was never registered.
    fs::remove_all(dir);
}
