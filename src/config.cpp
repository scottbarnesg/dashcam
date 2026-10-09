#include "config.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace {

std::string trim(const std::string& s) {
    auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// Returns false if the key is unknown or the value fails to parse (logged).
bool applyKeyValue(Config& config, const std::string& key, const std::string& value) {
    // Parses without touching out on failure.
    auto parseInt = [&](int& out) {
        try {
            size_t used = 0;
            int v = std::stoi(value, &used);
            if (used != value.size()) {
                return false;
            }
            out = v;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    };
    auto parseDouble = [&](double& out) {
        try {
            size_t used = 0;
            double v = std::stod(value, &used);
            if (used != value.size()) {
                return false;
            }
            out = v;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    };
    auto checkInt = [&](int& field, int min) {
        int parsed = 0;
        if (!parseInt(parsed) || parsed < min) {
            return false;
        }
        field = parsed;
        return true;
    };
    auto checkDouble = [&](double& field, double min) {
        double parsed = 0;
        if (!parseDouble(parsed) || parsed <= min) {
            return false;
        }
        field = parsed;
        return true;
    };

    if (key == "camera_backend") {
        if (value != "pi") {
            return false;
        }
        config.cameraBackend = value;
    } else if (key == "video_dir") {
        if (value.empty()) {
            return false;
        }
        config.videoDir = value;
    } else if (key == "no_motion_timeout_seconds") {
        if (!checkInt(config.noMotionTimeoutSeconds, 1)) {
            return false;
        }
    } else if (key == "idle_fps") {
        if (!checkDouble(config.idleFps, 0)) {
            return false;
        }
    } else if (key == "recording_fps") {
        if (!checkDouble(config.recordingFps, 0)) {
            return false;
        }
    } else if (key == "motion_threshold") {
        if (!checkInt(config.motionThreshold, 1)) {
            return false;
        }
    } else if (key == "segment_length_seconds") {
        if (!checkInt(config.segmentLengthSeconds, 1)) {
            return false;
        }
    } else if (key == "encoder") {
        if (value != "auto" && value != "hw" && value != "sw") {
            return false;
        }
        config.encoder = value;
    } else if (key == "video_bitrate_kbps") {
        if (!checkInt(config.videoBitrateKbps, 16)) {
            return false;
        }
    } else if (key == "video_gop_seconds") {
        if (!checkInt(config.videoGopSeconds, 1)) {
            return false;
        }
    } else {
        std::cerr << "Config: unknown key '" << key << "'" << std::endl;
        return false;
    }
    return true;
}

} // namespace

Config Config::load(const std::filesystem::path& path) {
    Config config;
    std::ifstream file(path);
    if (!file) {
        std::cout << "Config: no file at " << path << ", using defaults" << std::endl;
        return config;
    }
    std::string line;
    int lineNo = 0;
    while (std::getline(file, line)) {
        lineNo++;
        std::string stripped = trim(line);
        if (stripped.empty() || stripped[0] == '#') {
            continue;
        }
        auto eq = stripped.find('=');
        if (eq == std::string::npos) {
            std::cerr << "Config: line " << lineNo << ": expected 'key = value', skipping" << std::endl;
            continue;
        }
        std::string key = trim(stripped.substr(0, eq));
        std::string value = trim(stripped.substr(eq + 1));
        if (!applyKeyValue(config, key, value)) {
            std::cerr << "Config: line " << lineNo << ": invalid entry '" << stripped << "', keeping default" << std::endl;
        }
    }
    return config;
}
