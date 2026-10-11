#include "cleanup.hpp"
#include "manifest.hpp"
#include "naming.hpp"

#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace std::chrono;
using ms = std::chrono::milliseconds;

namespace {

ms hoursMs(int h) { return duration_cast<ms>(hours(h)); }

// Move a file's modification time back in time so its computed age is stable.
void setAge(const fs::path& p, hours age) {
    using FileClock = fs::file_time_type::clock;
    fs::last_write_time(p, FileClock::now() - age);
}

cleanup::Candidate cand(const std::string& file, std::uintmax_t size, ms age) {
    return cleanup::Candidate{file, size, age};
}

} // namespace

// ---- Pure selection logic -------------------------------------------------

TEST(SelectVictims, AgeRuleSelectsExpiredOldestFirst) {
    cleanup::Policy policy;
    policy.minFreeBytes = 0;               // Space rule off.
    policy.maxAge = hoursMs(40);
    policy.grace = minutes(1);

    std::vector<cleanup::Candidate> files = {
        cand("recent.mp4", 1000, hoursMs(1)),
        cand("old.mp4", 1000, hoursMs(100)),
        cand("mid.mp4", 1000, hoursMs(50)),
    };
    auto victims = cleanup::selectVictims(files, 1ULL << 40, policy);
    EXPECT_EQ(victims, (std::vector<std::string>{"old.mp4", "mid.mp4"})); // Oldest first.
}

TEST(SelectVictims, SpaceRuleDeletesOldestUntilFloorReached) {
    cleanup::Policy policy;
    policy.minFreeBytes = 2000;            // Need to reclaim 2000 bytes.
    policy.maxAge = ms(0);                 // Age rule off.
    policy.grace = ms(0);

    std::vector<cleanup::Candidate> files = {
        cand("newest.mp4", 1000, hoursMs(10)),
        cand("oldest.mp4", 1000, hoursMs(30)),
        cand("middle.mp4", 1000, hoursMs(20)),
    };
    auto victims = cleanup::selectVictims(files, 0, policy);
    EXPECT_EQ(victims, (std::vector<std::string>{"oldest.mp4", "middle.mp4"}));
}

TEST(SelectVictims, GraceProtectsFreshFilesUnderSpacePressure) {
    cleanup::Policy policy;
    policy.minFreeBytes = 100000;          // Can never be met.
    policy.maxAge = ms(0);
    policy.grace = hoursMs(1);

    std::vector<cleanup::Candidate> files = {
        cand("stale.mp4", 1000, hoursMs(10)),
        cand("fresh.mp4", 1000, minutes(10)),
    };
    auto victims = cleanup::selectVictims(files, 0, policy);
    EXPECT_EQ(victims, (std::vector<std::string>{"stale.mp4"})); // Fresh one is off-limits.
}

TEST(SelectVictims, NothingSelectedWhenFloorAlreadySatisfied) {
    cleanup::Policy policy;
    policy.minFreeBytes = 0;
    policy.maxAge = ms(0);

    std::vector<cleanup::Candidate> files = {cand("a.mp4", 1000, hoursMs(1000))};
    EXPECT_TRUE(cleanup::selectVictims(files, 1ULL << 40, policy).empty());
}

TEST(SelectVictims, UnknownAgeIsNeverSelected) {
    cleanup::Policy policy;
    policy.minFreeBytes = 100000;
    policy.maxAge = hoursMs(1);
    policy.grace = hoursMs(1);

    std::vector<cleanup::Candidate> files = {cand("stat_failed.mp4", 1000, ms(-1))};
    EXPECT_TRUE(cleanup::selectVictims(files, 0, policy).empty());
}

TEST(SelectVictims, DeterministicOrderOnAgeTie) {
    cleanup::Policy policy;
    policy.minFreeBytes = 0;
    policy.maxAge = hoursMs(40);
    policy.grace = ms(0);

    std::vector<cleanup::Candidate> files = {
        cand("dashcam_seg_b.mp4", 10, hoursMs(100)),
        cand("dashcam_seg_a.mp4", 10, hoursMs(100)),
    };
    auto victims = cleanup::selectVictims(files, 1ULL << 40, policy);
    EXPECT_EQ(victims, (std::vector<std::string>{"dashcam_seg_a.mp4", "dashcam_seg_b.mp4"}));
}

// ---- enforce() against real files + manifest ------------------------------

class CleanupEnforceTest : public ::testing::Test {
    protected:
        fs::path dir = "test_cleanup";
        void SetUp() override {
            fs::remove_all(dir);
            fs::create_directories(dir);
        }
        void TearDown() override { fs::remove_all(dir); }

        fs::path recording(Manifest& m, unsigned long seq, hours age) {
            fs::path p = dir / naming::fileName(system_clock::now(), "seg", seq);
            std::ofstream(p) << "recording-" << seq;
            m.addRecording(p, "seg", naming::timestamp(system_clock::now()), 1.0);
            setAge(p, age);
            return p;
        }
};

TEST_F(CleanupEnforceTest, AgeRetentionDeletesOldAndPersistsManifest) {
    Manifest m(dir);
    auto old1 = recording(m, 1, hours(100));
    auto old2 = recording(m, 2, hours(90));
    auto recent = recording(m, 3, hours(0));
    ASSERT_TRUE(m.save());

    cleanup::Policy policy;
    policy.minFreeBytes = 0;               // Age rule only.
    policy.maxAge = hours(50);

    auto result = cleanup::enforce(m, dir, policy);
    EXPECT_EQ(result.deleted, 2u);
    EXPECT_FALSE(fs::exists(old1));
    EXPECT_FALSE(fs::exists(old2));
    EXPECT_TRUE(fs::exists(recent));
    EXPECT_EQ(m.size(), 1u);

    Manifest reloaded(dir);                // Deletion persisted atomically.
    ASSERT_TRUE(reloaded.load());
    EXPECT_EQ(reloaded.size(), 1u);
    EXPECT_TRUE(reloaded.contains(recent.filename().string()));
}

TEST_F(CleanupEnforceTest, OpenSegmentWithSentinelIsNeverDeleted) {
    Manifest m(dir);
    auto open = recording(m, 1, hours(100));
    std::ofstream(open.string() + ".writing").close(); // Belt-and-braces guard.

    cleanup::Policy policy;
    policy.minFreeBytes = 0;
    policy.maxAge = hours(1);

    auto result = cleanup::enforce(m, dir, policy);
    EXPECT_EQ(result.deleted, 0u);
    EXPECT_TRUE(fs::exists(open));
    EXPECT_EQ(m.size(), 1u);
}

TEST_F(CleanupEnforceTest, DisabledPolicyIsANoOp) {
    Manifest m(dir);
    auto old = recording(m, 1, hours(1000));

    cleanup::Policy policy;
    policy.enabled = false;
    policy.minFreeBytes = 0;
    policy.maxAge = hours(1);

    auto result = cleanup::enforce(m, dir, policy);
    EXPECT_EQ(result.deleted, 0u);
    EXPECT_TRUE(fs::exists(old));
}

TEST_F(CleanupEnforceTest, MissingFileDoesNotCrashAndKeepsEntry) {
    Manifest m(dir);
    auto gone = recording(m, 1, hours(100));
    fs::remove(gone);                      // Deleted out from under the manifest.

    cleanup::Policy policy;
    policy.minFreeBytes = 0;
    policy.maxAge = hours(1);

    auto result = cleanup::enforce(m, dir, policy);
    EXPECT_EQ(result.deleted, 0u);
    EXPECT_EQ(m.size(), 1u);               // Entry left for a later rebuild.
}
