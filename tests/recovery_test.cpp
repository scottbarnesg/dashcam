#include "recovery.hpp"

#include <fstream>
#include <gtest/gtest.h>

namespace fs = std::filesystem;

class RecoveryTest : public ::testing::Test {
    protected:
        fs::path dir = "test_recovery";
        void SetUp() override {
            fs::remove_all(dir);
            fs::create_directories(dir);
        }
        void TearDown() override { fs::remove_all(dir); }
};

TEST_F(RecoveryTest, QuarantinesOpenSegmentOnly) {
    // Simulate a power cut: one closed segment, one open segment with sentinel.
    std::ofstream(dir / "dashcam_seg_20261007-120000_0001.mp4") << "closed";
    std::ofstream(dir / "dashcam_seg_20261007-120200_0002.mp4") << "cut mid write";
    std::ofstream(dir / "dashcam_seg_20261007-120200_0002.mp4.writing");

    EXPECT_EQ(quarantineIncompleteSegments(dir), 1u);

    EXPECT_FALSE(fs::exists(dir / "dashcam_seg_20261007-120200_0002.mp4"));
    EXPECT_FALSE(fs::exists(dir / "dashcam_seg_20261007-120200_0002.mp4.writing"));
    EXPECT_TRUE(fs::exists(dir / "quarantine/dashcam_seg_20261007-120200_0002.mp4"));
    EXPECT_TRUE(fs::exists(dir / "dashcam_seg_20261007-120000_0001.mp4"));

    EXPECT_EQ(quarantineIncompleteSegments(dir), 0u);          // Idempotent.
    EXPECT_EQ(quarantineIncompleteSegments("no_such_dir"), 0u); // Safe on missing dir.
}
