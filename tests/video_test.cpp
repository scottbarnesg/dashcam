#include "manifest.hpp"
#include "recovery.hpp"
#include "video.hpp"
#include "test.hpp"

#include <chrono>
#include <filesystem>
#include <opencv2/videoio.hpp>
#include <thread>

#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

using Clock = std::chrono::system_clock;

void feedFrames(VideoWriter& w, int n, int width = 160, int height = 120) {
    for (int i = 0; i < n; i++) {
        cv::Mat frame(height, width, CV_8UC3, cv::Scalar((i * 7) % 256, 40, 90));
        w.addFrame(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

int countVideos(const fs::path& dir) {
    int n = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() == ".mp4") {
            n++;
        }
    }
    return n;
}

int main() {
    // Basic: destructor flushes, file is valid, empty frames ignored.
    fs::path dir = "test_videos_basic";
    fs::remove_all(dir);
    {
        VideoWriter writer(dir, VideoWriter::SegmentOptions{});
        feedFrames(writer, 40);
        writer.addFrame(cv::Mat());
    }
    CHECK_EQ(countVideos(dir), 1);
    fs::path produced;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() == ".mp4") {
            produced = e.path();
        }
    }
    CHECK(!produced.empty());
    CHECK(fs::file_size(produced) > 0);
    cv::VideoCapture cap(produced);
    CHECK(cap.isOpened());
    int frames = 0;
    cv::Mat frame;
    while (cap.read(frame)) {
        frames++;
    }
    CHECK(frames >= 30);

    // Segments: with a 1s segment length, ~1.6s of frames must produce
    // multiple closed, manifest-registered segments and no leftover sentinels.
    fs::path segDir = "test_videos_segments";
    fs::remove_all(segDir);
    int segmentsInManifest = 0;
    {
        Manifest manifest(segDir);
        VideoWriter::SegmentOptions opts;
        opts.lengthSeconds = 1;
        opts.manifest = &manifest;
        VideoWriter writer(segDir, opts);
        feedFrames(writer, 80); // ~1.6s at 20fps pacing
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
        feedFrames(writer, 20);
    } // Joined: segment count is final.
    segmentsInManifest = countVideos(segDir);
    CHECK(segmentsInManifest >= 2);
    {
        Manifest manifest(segDir);
        CHECK(manifest.load());
        CHECK_EQ(manifest.size(), std::size_t(segmentsInManifest));
        bool allHashed = true;
        for (const ManifestEntry& e : manifest.entries()) {
            allHashed = allHashed && e.sha256.size() == 64 && e.sizeBytes > 0;
            CHECK(!fs::exists(segDir / (e.file + ".writing"))); // No stale sentinels.
        }
        CHECK(allHashed);
    }

    // Power-cut simulation: a child process is SIGKILLed mid-segment (exactly
    // like the vehicle losing power); the sentinel must remain and recovery
    // must quarantine the never-closed file.
    fs::path cutDir = "test_videos_cut";
    fs::remove_all(cutDir);
    {
        pid_t child = fork();
        if (child == 0) {
            VideoWriter::SegmentOptions opts;
            opts.lengthSeconds = 1000; // One long segment so the cut lands mid-segment.
            VideoWriter writer(cutDir, opts);
            feedFrames(writer, 200);
            _exit(0);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        kill(child, SIGKILL);
        int status = 0;
        waitpid(child, &status, 0);
    }
    bool sentinelLeft = false;
    for (const auto& e : fs::recursive_directory_iterator(cutDir)) {
        if (e.path().extension() == ".writing") {
            sentinelLeft = true;
        }
    }
    CHECK(sentinelLeft); // Kill landed while the segment was open (near-certain).
    CHECK_EQ(quarantineIncompleteSegments(cutDir), std::size_t(1));
    CHECK(fs::exists(cutDir / "quarantine"));
    CHECK_EQ(countVideos(cutDir), 0); // Open segment left the main dir...
    {
        Manifest manifest(cutDir);
        manifest.load(); // Nothing registered: the killed segment never closed.
        CHECK_EQ(manifest.size(), std::size_t(0));
    }

    fs::remove_all(dir);
    fs::remove_all(segDir);
    fs::remove_all(cutDir);
    TEST_RESULT();
}
