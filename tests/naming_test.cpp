#include "naming.hpp"

#include <gtest/gtest.h>

TEST(Naming, FileNameIsUniqueAndMatchesPattern) {
    auto t = std::chrono::system_clock::now();
    std::string a = naming::fileName(t, "seg", 1);
    std::string b = naming::fileName(t, "seg", 2);
    EXPECT_NE(a, b); // Same start time, different sequence -> unique.
    EXPECT_TRUE(naming::matches(a));
    EXPECT_TRUE(naming::matches(naming::fileName(t, "event", 42)));
}

TEST(Naming, MatchesRejectsForeignNames) {
    EXPECT_FALSE(naming::matches("random.mp4"));
    EXPECT_FALSE(naming::matches("dashcam_seg_2026-10-07_0001.mp4"));
    EXPECT_FALSE(naming::matches("dashcam_seg_20261007-180000_1.mp4"));
    EXPECT_FALSE(naming::matches("dashcam_seg_20261007-180000_0001.txt"));
}

TEST(Naming, SortableByName) {
    auto t = std::chrono::system_clock::now();
    std::string early = naming::fileName(t - std::chrono::hours(2), "seg", 7);
    std::string late = naming::fileName(t, "seg", 7);
    EXPECT_LT(early, late);
}

TEST(Naming, TimestampFormat) {
    std::string stamp = naming::timestamp(std::chrono::system_clock::now());
    EXPECT_EQ(stamp.size(), 15u);
    EXPECT_EQ(stamp[8], '-');
}
