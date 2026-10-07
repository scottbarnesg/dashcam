#include "naming.hpp"
#include "test.hpp"

int main() {
    auto t = std::chrono::system_clock::now();
    std::string a = naming::fileName(t, "seg", 1);
    std::string b = naming::fileName(t, "seg", 2);
    CHECK(naming::matches(a));
    CHECK(naming::matches(naming::fileName(t, "event", 42)));
    CHECK(!naming::matches("random.mp4"));
    CHECK(!naming::matches("dashcam_seg_2026-10-07_0001.mp4"));
    CHECK(!naming::matches("dashcam_seg_20261007-180000_1.mp4"));
    CHECK(!naming::matches("dashcam_seg_20261007-180000_0001.txt"));

    // Same start time, different sequence -> unique names.
    CHECK(a != b);

    // Sortable: earlier time sorts before later time with same sequence.
    std::string early = naming::fileName(t - std::chrono::hours(2), "seg", 7);
    std::string late = naming::fileName(t, "seg", 7);
    CHECK(early < late);

    // Timestamp round-trips the fixed format.
    CHECK_EQ(naming::timestamp(t).size(), std::size_t(15));
    CHECK_EQ(naming::timestamp(t)[8], '-');

    TEST_RESULT();
}
