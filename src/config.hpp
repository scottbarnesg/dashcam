#include <filesystem>
#include <string>

#ifndef CONFIG_H
#define CONFIG_H

// Runtime configuration (BACKLOG item 4). Loaded from a "key = value" text file;
// unknown keys and invalid values are logged and the built-in default is kept.
struct Config {
    std::string cameraBackend = "usb";     // usb | pi
    std::filesystem::path videoDir = "videos";
    int noMotionTimeoutSeconds = 120;      // N for the driving state machine
    double idleFps = 2.0;
    double recordingFps = 12.0;
    int motionThreshold = 10000;           // min contour area (px) to count as motion
    int segmentLengthSeconds = 120;        // power-loss-safe segment size (item 3)

    // Parses path; missing file -> defaults. Invalid entries are reported to
    // stderr but never fatal.
    static Config load(const std::filesystem::path& path);
};

#endif
