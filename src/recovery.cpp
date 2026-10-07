#include "recovery.hpp"

#include <iostream>

namespace fs = std::filesystem;

std::size_t quarantineIncompleteSegments(const fs::path& videoDir) {
    std::size_t count = 0;
    std::error_code ec;
    if (!fs::is_directory(videoDir)) {
        return 0;
    }
    for (const auto& entry : fs::directory_iterator(videoDir, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string name = entry.path().filename().string();
        const std::string suffix = ".mp4.writing";
        if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
            continue;
        }
        fs::path videoPath = entry.path();
        videoPath.replace_extension(""); // strip .writing -> .mp4
        fs::path quarantineDir = videoDir / "quarantine";
        fs::create_directories(quarantineDir, ec);
        fs::path target = quarantineDir / videoPath.filename();
        fs::rename(videoPath, target, ec);
        bool moved = !ec;
        if (!moved) {
            std::cerr << "Recovery: could not quarantine " << videoPath << ": " << ec.message() << std::endl;
            continue;
        }
        fs::remove(entry.path(), ec); // Remove the sentinel.
        std::cout << "Recovery: quarantined incomplete segment " << videoPath.filename() << std::endl;
        count++;
    }
    return count;
}
