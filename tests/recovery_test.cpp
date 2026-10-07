#include "recovery.hpp"
#include "test.hpp"

#include <fstream>

namespace fs = std::filesystem;

int main() {
    fs::path dir = "test_recovery";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // Simulate a power cut: one closed segment, one open segment with sentinel.
    std::ofstream(dir / "dashcam_seg_20261007-120000_0001.mp4") << "closed";
    std::ofstream(dir / "dashcam_seg_20261007-120200_0002.mp4") << "cut mid write";
    std::ofstream(dir / "dashcam_seg_20261007-120200_0002.mp4.writing");

    CHECK_EQ(quarantineIncompleteSegments(dir), std::size_t(1));

    // Open segment moved to quarantine, sentinel gone, closed file untouched.
    CHECK(!fs::exists(dir / "dashcam_seg_20261007-120200_0002.mp4"));
    CHECK(!fs::exists(dir / "dashcam_seg_20261007-120200_0002.mp4.writing"));
    CHECK(fs::exists(dir / "quarantine/dashcam_seg_20261007-120200_0002.mp4"));
    CHECK(fs::exists(dir / "dashcam_seg_20261007-120000_0001.mp4"));

    // Idempotent; also safe on missing directory.
    CHECK_EQ(quarantineIncompleteSegments(dir), std::size_t(0));
    CHECK_EQ(quarantineIncompleteSegments("no_such_dir"), std::size_t(0));

    fs::remove_all(dir);
    TEST_RESULT();
}
