// End-to-end pipeline test (no camera hardware): SimulatedCamera -> MotionDetector
// -> DrivingController -> segmented VideoWriter -> Manifest -> recovery, exactly the
// wiring used by run/dashcam.cpp.
#include "driving.hpp"
#include "manifest.hpp"
#include "motion.hpp"
#include "recovery.hpp"
#include "simulated_camera.hpp"
#include "video.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

namespace fs = std::filesystem;
using Clock = std::chrono::system_clock;

namespace {
DrivingController::Params testParams() {
    DrivingController::Params params;
    params.noMotionTimeout = std::chrono::seconds(1);
    params.idleFps = 1000;
    params.recordingFps = 1000;
    return params;
}
}

class PipelineTest : public ::testing::Test {
    protected:
        fs::path dir = "test_e2e";
        SimulatedCamera camera;
        MotionDetector detector{5000};
        DrivingController controller{testParams()};
        Manifest manifest{dir};
        std::unique_ptr<VideoWriter> writer;

        void SetUp() override {
            fs::remove_all(dir);
            fs::create_directories(dir);
            ASSERT_EQ(quarantineIncompleteSegments(dir), 0u);
            manifest.load();
        }
        void TearDown() override {
            writer.reset();
            fs::remove_all(dir);
        }

        void recordFor(std::chrono::milliseconds duration) {
            VideoWriter::SegmentOptions seg;
            seg.lengthSeconds = 1;
            seg.manifest = &manifest;
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
        }
};

TEST_F(PipelineTest, RecordsWhileDrivingAndStopsWhenParked) {
    // Parked, static: nothing recorded.
    camera.setMoving(false);
    recordFor(std::chrono::milliseconds(300));
    EXPECT_EQ(writer, nullptr);

    // Drive (motion): recording starts and segments roll.
    camera.setMoving(true);
    recordFor(std::chrono::milliseconds(1500));
    ASSERT_NE(writer, nullptr);

    // Stopped: after N=1s with no motion, recording stops and file closes.
    camera.setMoving(false);
    recordFor(std::chrono::milliseconds(1500));
    EXPECT_EQ(writer, nullptr);

    EXPECT_GE(manifest.size(), 2u); // Rolled segments plus the final closed one.
    for (const ManifestEntry& e : manifest.entries()) {
        EXPECT_TRUE(fs::exists(dir / e.file));
        EXPECT_EQ(e.sha256.size(), 64u);
    }
}

TEST_F(PipelineTest, BootRecoveryRegistersOrphansAndQuarantinesCut) {
    std::ofstream(dir / "dashcam_seg_20261007-090000_8888.mp4") << "orphan";
    std::ofstream(dir / "dashcam_seg_20261007-090001_8889.mp4") << "cut";
    std::ofstream(dir / "dashcam_seg_20261007-090001_8889.mp4.writing");

    EXPECT_EQ(quarantineIncompleteSegments(dir), 1u);
    EXPECT_EQ(manifest.rebuildFromDisk(), 1u); // The orphan, not the quarantined file.
}
