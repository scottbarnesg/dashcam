// End-to-end pipeline test (no camera hardware): the real Pipeline class
// (ingest/motion/record threads) with a SimulatedCamera, exercising the same
// wiring as run/dashcam.cpp, plus the boot-recovery path.
#include "driving.hpp"
#include "manifest.hpp"
#include "motion.hpp"
#include "pipeline.hpp"
#include "recovery.hpp"
#include "simulated_camera.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

namespace fs = std::filesystem;

namespace {

bool waitFor(const std::function<bool()>& pred, int timeoutMs) {
    for (int waited = 0; waited < timeoutMs; waited += 20) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return pred();
}

Pipeline::Options fastOptions(const fs::path& dir, Manifest* manifest) {
    Pipeline::Options options;
    options.motionThreshold = 5000;
    options.driving.noMotionTimeout = std::chrono::seconds(1);
    options.driving.idleFps = 200;
    options.driving.recordingFps = 200;
    options.videoDir = dir;
    options.segments.lengthSeconds = 1;
    options.segments.recordingFps = 30;
    options.segments.context = "seg";
    options.segments.manifest = manifest;
    return options;
}

} // namespace

TEST(PipelineTest, RecordsWhileDrivingAndStopsWhenParked) {
    fs::path dir = "test_e2e";
    fs::remove_all(dir);
    fs::create_directories(dir);
    ASSERT_EQ(quarantineIncompleteSegments(dir), 0u);
    Manifest manifest(dir);
    manifest.load(); // Fresh directory: nothing to load yet; must not fail the test.

    auto camera = std::make_unique<SimulatedCamera>();
    SimulatedCamera* cam = camera.get();
    Pipeline pipeline(std::move(camera), fastOptions(dir, &manifest));

    // Parked, static: nothing recorded.
    cam->setMoving(false);
    pipeline.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    EXPECT_FALSE(pipeline.recording());

    // Drive (motion): recording starts.
    cam->setMoving(true);
    ASSERT_TRUE(waitFor([&] { return pipeline.recording(); }, 3000));
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    // Stopped: after N=1s with no motion, recording stops and segments close.
    cam->setMoving(false);
    ASSERT_TRUE(waitFor([&] { return !pipeline.recording(); }, 5000));
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // let the recorder close out
    pipeline.stop();

    EXPECT_GE(manifest.size(), 2u); // Rolled segments plus the final closed one.
    for (const ManifestEntry& e : manifest.entries()) {
        EXPECT_TRUE(fs::exists(dir / e.file));
        EXPECT_EQ(e.sha256.size(), 64u);
        EXPECT_FALSE(fs::exists(dir / (e.file + ".writing")));
    }
    fs::remove_all(dir);
}

TEST(PipelineTest, StagedQueuesNeverBlockIntake) {
    // A deliberately slow pipeline: tiny record queue, motion at 200 fps.
    // Intake must keep flowing throughout (no deadlock); recorded frames are
    // dropped with a count, never a stall.
    fs::path dir = "test_e2e_flow";
    fs::remove_all(dir);
    fs::create_directories(dir);
    Manifest manifest(dir);
    Pipeline::Options options = fastOptions(dir, &manifest);
    options.recordQueueCapacity = 2;
    auto camera = std::make_unique<SimulatedCamera>();
    SimulatedCamera* cam = camera.get();
    cam->setMoving(true);
    Pipeline pipeline(std::move(camera), options);
    pipeline.start();
    ASSERT_TRUE(waitFor([&] { return pipeline.recording(); }, 3000));
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    pipeline.stop(); // Must join every thread despite frames still in flight.
    EXPECT_GE(manifest.size(), 1u); // A segment made it through despite queue pressure.
    fs::remove_all(dir);
}

TEST(PipelineTest, BootRecoveryRegistersOrphansAndQuarantinesCut) {
    fs::path dir = "test_e2e_recovery";
    fs::remove_all(dir);
    fs::create_directories(dir);
    Manifest manifest(dir);
    std::ofstream(dir / "dashcam_seg_20261007-090000_8888.mp4") << "orphan";
    std::ofstream(dir / "dashcam_seg_20261007-090001_8889.mp4") << "cut";
    std::ofstream(dir / "dashcam_seg_20261007-090001_8889.mp4.writing");

    EXPECT_EQ(quarantineIncompleteSegments(dir), 1u);
    EXPECT_EQ(manifest.rebuildFromDisk(), 1u); // The orphan, not the quarantined file.
    fs::remove_all(dir);
}
