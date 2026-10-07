// End-to-end pipeline test (no camera hardware): SimulatedCamera -> MotionDetector
// -> DrivingController -> segmented VideoWriter -> Manifest -> recovery, exactly the
// wiring used by run/dashcam.cpp.
#include "camera.hpp"
#include "camera_impl.hpp"
#include "driving.hpp"
#include "manifest.hpp"
#include "motion.hpp"
#include "recovery.hpp"
#include "test.hpp"
#include "video.hpp"

#include <chrono>
#include <fstream>
#include <filesystem>
#include <memory>
#include <thread>

namespace fs = std::filesystem;
using Clock = std::chrono::system_clock;

int main() {
    quarantineIncompleteSegments("no_such_dir"); // Smoke: safe on missing dir.

    fs::path dir = "test_e2e";
    fs::remove_all(dir);

    SimulatedCamera camera;
    MotionDetector detector(5000);
    DrivingController::Params params;
    params.noMotionTimeout = std::chrono::seconds(1);
    params.idleFps = 1000;
    params.recordingFps = 1000;
    DrivingController controller(params);
    Manifest manifest(dir);
    quarantineIncompleteSegments(dir);
    manifest.load();

    VideoWriter::SegmentOptions seg;
    seg.lengthSeconds = 1;
    seg.manifest = &manifest;
    std::unique_ptr<VideoWriter> writer;

    auto recordFor = [&](std::chrono::milliseconds duration) {
        auto end = Clock::now() + duration;
        while (Clock::now() < end) {
            auto now = Clock::now();
            auto frame = camera.captureImage();
            detector.addFrame(frame);
            bool recording = controller.onFrame(now, detector.motionDetected());
            if (recording && !writer) {
                writer = std::make_unique<VideoWriter>(dir, seg);
            }
            if (recording) {
                writer->addFrame(frame);
            } else if (writer) {
                writer.reset();
                detector.reset();
            }
        }
    };

    // Parked, static: nothing recorded.
    camera.setMoving(false);
    recordFor(std::chrono::milliseconds(300));
    CHECK(!writer);

    // Drive (motion): recording starts and segments roll.
    camera.setMoving(true);
    recordFor(std::chrono::milliseconds(1500));
    CHECK(writer != nullptr);

    // Stopped: after N=1s with no motion, recording stops and file is closed.
    camera.setMoving(false);
    recordFor(std::chrono::milliseconds(1500));
    CHECK(writer == nullptr);

    CHECK(manifest.size() >= 2); // At least one rolled segment plus the final closed one.
    for (const ManifestEntry& e : manifest.entries()) {
        CHECK(fs::exists(dir / e.file));
        CHECK(e.sha256.size() == 64);
    }

    // Boot after a "power cut": register a stray unmanifested file + a torn segment.
    writer.reset();
    {
        std::ofstream(dir / "dashcam_seg_20261007-090000_8888.mp4") << "orphan";
        std::ofstream(dir / "dashcam_seg_20261007-090001_8889.mp4") << "cut";
        std::ofstream(dir / "dashcam_seg_20261007-090001_8889.mp4.writing");
    }
    CHECK_EQ(quarantineIncompleteSegments(dir), std::size_t(1));
    CHECK_EQ(manifest.rebuildFromDisk(), std::size_t(1)); // The orphan, not the quarantined file.

    fs::remove_all(dir);
    TEST_RESULT();
}
