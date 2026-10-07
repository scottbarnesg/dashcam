#include "config.hpp"
#include "test.hpp"

#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

int main() {
    // Defaults when file is missing.
    Config defaults = Config::load("does_not_exist.conf");
    CHECK_EQ(defaults.cameraBackend, std::string("usb"));
    CHECK_EQ(defaults.noMotionTimeoutSeconds, 120);

    fs::path p = "test_config.conf";
    {
        std::ofstream f(p);
        f << "# dashcam config\n"
          << "\n"
          << "camera_backend = sim\n"
          << "video_dir = /tmp/vids\n"
          << "no_motion_timeout_seconds = 45\n"
          << "idle_fps = 3.5\n"
          << "motion_threshold = 500\n"
          << "badline_no_equals\n"
          << "no_motion_timeout_seconds = notanumber\n"
          << "no_motion_timeout_seconds = -5\n"
          << "camera_backend = telepathy\n"
          << "unknown_key = 1\n";
    }
    Config c = Config::load(p);
    CHECK_EQ(c.cameraBackend, std::string("sim"));        // valid line applied
    CHECK_EQ(c.videoDir.string(), std::string("/tmp/vids"));
    CHECK_EQ(c.noMotionTimeoutSeconds, 45);               // valid; invalid repeats keep 45
    CHECK_EQ(c.idleFps, 3.5);
    CHECK_EQ(c.motionThreshold, 500);
    CHECK_EQ(c.segmentLengthSeconds, 120);                // untouched default

    fs::remove(p);
    TEST_RESULT();
}
