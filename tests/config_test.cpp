#include "config.hpp"

#include <fstream>
#include <gtest/gtest.h>

namespace fs = std::filesystem;

TEST(Config, DefaultsWhenFileMissing) {
    Config defaults = Config::load("does_not_exist.conf");
    EXPECT_EQ(defaults.cameraBackend, "pi");
    EXPECT_EQ(defaults.noMotionTimeoutSeconds, 120);
    EXPECT_DOUBLE_EQ(defaults.recordingFps, 30.0);
}

class ConfigTest : public ::testing::Test {
    protected:
        void write(const std::string& contents) {
            std::ofstream(p) << contents;
        }
        fs::path p = "test_config.conf";
        void TearDown() override { fs::remove(p); }
};

TEST_F(ConfigTest, ValidAndInvalidEntries) {
    write("# dashcam config\n"
          "\n"
          "camera_backend = pi\n"
          "video_dir = /tmp/vids\n"
          "no_motion_timeout_seconds = 45\n"
          "idle_fps = 3.5\n"
          "motion_threshold = 500\n"
          "badline_no_equals\n"
          "no_motion_timeout_seconds = notanumber\n"
          "no_motion_timeout_seconds = -5\n"
          "camera_backend = telepathy\n"
          "unknown_key = 1\n");
    Config c = Config::load(p);
    EXPECT_EQ(c.cameraBackend, "pi");                  // valid line applied
    EXPECT_EQ(c.videoDir.string(), "/tmp/vids");
    EXPECT_EQ(c.noMotionTimeoutSeconds, 45);           // invalid repeats keep 45
    EXPECT_DOUBLE_EQ(c.idleFps, 3.5);
    EXPECT_EQ(c.motionThreshold, 500);
    EXPECT_EQ(c.segmentLengthSeconds, 120);            // untouched default
}

TEST_F(ConfigTest, CameraOrientation) {
    write("camera_orientation = 90\n"
          "camera_orientation = mirror-270\n"
          "camera_orientation = 45\n"); // invalid: last good value kept
    Config c = Config::load(p);
    EXPECT_EQ(c.cameraOrientation, CameraOrientation::Mirror270);
    EXPECT_EQ(Config::load("does_not_exist.conf").cameraOrientation, CameraOrientation::Auto);
}

TEST_F(ConfigTest, EncoderSettings) {
    write("encoder = hw\n"
          "video_bitrate_kbps = 8000\n"
          "video_gop_seconds = 3\n"
          "encoder = bogus\n"                     // invalid: last good value kept
          "video_bitrate_kbps = 5\n"              // below minimum: kept out
          "video_gop_seconds = zero\n");
    Config c = Config::load(p);
    EXPECT_EQ(c.encoder, "hw");
    EXPECT_EQ(c.videoBitrateKbps, 8000);
    EXPECT_EQ(c.videoGopSeconds, 3);
}
