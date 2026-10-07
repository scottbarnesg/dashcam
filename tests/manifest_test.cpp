#include "manifest.hpp"
#include "naming.hpp"

#include <fstream>
#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace {
fs::path makeVideo(const fs::path& dir, const std::string& context, unsigned long seq,
                   const std::string& content, std::chrono::system_clock::time_point t) {
    fs::path p = dir / naming::fileName(t, context, seq);
    std::ofstream(p) << content;
    return p;
}
}

class ManifestTest : public ::testing::Test {
    protected:
        fs::path dir = "test_manifest";
        void SetUp() override {
            fs::remove_all(dir);
            fs::create_directories(dir);
        }
        void TearDown() override { fs::remove_all(dir); }
};

TEST_F(ManifestTest, Sha256KnownAnswer) {
    fs::path abc = dir / "abc.bin";
    std::ofstream(abc) << "abc";
    EXPECT_EQ(sha256File(abc),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"); // FIPS 180-4
}

TEST_F(ManifestTest, RebuildLoadSaveRoundTrip) {
    auto t = std::chrono::system_clock::now();
    auto v1 = makeVideo(dir, "seg", 1, "recording-one", t - std::chrono::hours(1));
    auto v2 = makeVideo(dir, "seg", 2, "recording-two", t);

    {
        Manifest m(dir);
        EXPECT_FALSE(m.load());                  // No manifest yet.
        EXPECT_EQ(m.rebuildFromDisk(), 2u);
        EXPECT_EQ(m.size(), 2u);

        const ManifestEntry* e = m.find(v1.filename().string());
        ASSERT_NE(e, nullptr);
        EXPECT_EQ(e->sizeBytes, 13u);
        EXPECT_EQ(e->context, "seg");
        EXPECT_EQ(e->state, UploadState::Local);
        EXPECT_EQ(e->sha256.size(), 64u);

        m.setState(v2.filename().string(), UploadState::UploadPending);
        EXPECT_TRUE(m.save());
    }
    Manifest m(dir);
    ASSERT_TRUE(m.load());
    const ManifestEntry* e = m.find(v2.filename().string());
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->state, UploadState::UploadPending);
    EXPECT_EQ(m.rebuildFromDisk(), 0u);          // Idempotent.
    EXPECT_TRUE(m.remove(v1.filename().string()));
    EXPECT_FALSE(m.remove("nope.mp4"));
}

TEST_F(ManifestTest, TornTrailingLineSurvivesPowerCut) {
    auto t = std::chrono::system_clock::now();
    auto v2 = makeVideo(dir, "seg", 2, "recording-two", t);
    {
        Manifest m(dir);
        m.rebuildFromDisk();
        std::ofstream f(dir / "manifest.jsonl", std::ios::app);
        f << "{\"file\": \"dashcam_seg_20261007-120000_9999.mp4\", \"sha2"; // torn write
    }
    Manifest m(dir);
    EXPECT_TRUE(m.load());
    EXPECT_EQ(m.size(), 1u);                     // Torn line dropped, good line kept.
    EXPECT_EQ(m.rebuildFromDisk(), 0u);

    fs::remove(v2);
    m.remove(v2.filename().string());
    makeVideo(dir, "seg", 2, "recording-two-restored", t);
    EXPECT_EQ(m.rebuildFromDisk(), 1u);          // Re-registered from disk.
}
