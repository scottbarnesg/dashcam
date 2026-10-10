#include <filesystem>
#include <string>

#include "orientation.hpp"

#ifndef CONFIG_H
#define CONFIG_H

// Runtime configuration (BACKLOG item 4). Loaded from a "key = value" text file;
// unknown keys and invalid values are logged and the built-in default is kept.
struct Config {
    std::string cameraBackend = "pi";      // pi (libcamera)
    // Frame orientation correction (item 7): "auto" takes what the sensor
    // reports; an explicit value overrides physical mounting differences.
    CameraOrientation cameraOrientation = CameraOrientation::Auto;
    std::filesystem::path videoDir = "videos";
    int noMotionTimeoutSeconds = 120;      // N for the driving state machine
    double idleFps = 5.0;
    double recordingFps = 30.0;
    int motionThreshold = 10000;           // min contour area (px) to count as motion
    int segmentLengthSeconds = 120;        // power-loss-safe segment size (item 3)
    std::string encoder = "auto";          // auto | hw | sw (hw = Pi V4L2 H.264 codec)
    int videoBitrateKbps = 5000;           // hardware encoder target bitrate
    int videoGopSeconds = 2;               // hardware encoder IDR/fragment interval

    // Parses path; missing file -> defaults. Invalid entries are reported to
    // stderr but never fatal.
    static Config load(const std::filesystem::path& path);
};

#endif
