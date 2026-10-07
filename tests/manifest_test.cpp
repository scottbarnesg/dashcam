#include "manifest.hpp"
#include "naming.hpp"
#include "test.hpp"

#include <fstream>

namespace fs = std::filesystem;

fs::path makeVideo(const fs::path& dir, const std::string& context, unsigned long seq,
                   const std::string& content, std::chrono::system_clock::time_point t) {
    fs::path p = dir / naming::fileName(t, context, seq);
    std::ofstream(p) << content;
    return p;
}

int main() {
    fs::path dir = "test_manifest";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto t = std::chrono::system_clock::now();

    // Known-answer SHA-256 test (FIPS 180-4: "abc").
    {
        fs::path abc = dir / "abc.bin";
        std::ofstream(abc) << "abc";
        CHECK_EQ(sha256File(abc),
                 std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    }

    auto v1 = makeVideo(dir, "seg", 1, "recording-one", t - std::chrono::hours(1));
    auto v2 = makeVideo(dir, "seg", 2, "recording-two", t);

    {
        Manifest m(dir);
        CHECK(!m.load()); // No manifest yet.
        CHECK_EQ(m.rebuildFromDisk(), std::size_t(2));
        CHECK_EQ(m.size(), std::size_t(2));

        const ManifestEntry* e = m.find(v1.filename().string());
        CHECK(e != nullptr);
        if (e) {
            CHECK_EQ(e->sizeBytes, std::uintmax_t(13));
            CHECK_EQ(e->context, std::string("seg"));
            CHECK(e->state == UploadState::Local);
            CHECK_EQ(e->sha256.size(), std::size_t(64));
        }

        m.setState(v2.filename().string(), UploadState::UploadPending);
        CHECK(m.save());
    }

    // Reload: state survived the round trip.
    {
        Manifest m(dir);
        CHECK(m.load());
        CHECK_EQ(m.size(), std::size_t(2));
        const ManifestEntry* e = m.find(v2.filename().string());
        CHECK(e != nullptr);
        if (e) {
            CHECK(e->state == UploadState::UploadPending);
        }

        // Rebuild is idempotent: nothing unregistered.
        CHECK_EQ(m.rebuildFromDisk(), std::size_t(0));

        // Delete flows through remove().
        CHECK(m.remove(v1.filename().string()));
        CHECK(!m.remove("nope.mp4"));
    }

    // Crash safety: a torn trailing line (power cut mid-write) is skipped,
    // and rebuild re-registers whatever the torn line lost.
    {
        {
            Manifest m(dir);
            m.load();
            m.save();
            std::ofstream f(dir / "manifest.jsonl", std::ios::app);
            f << "{\"file\": \"dashcam_seg_20261007-120000_9999.mp4\", \"sha2"; // torn write
        }
        Manifest m(dir);
        CHECK(m.load());
        CHECK_EQ(m.size(), std::size_t(2)); // Torn line dropped; both saved entries kept.
        CHECK_EQ(m.rebuildFromDisk(), std::size_t(0)); // Nothing unregistered on disk.

        // A registered file deleted from disk gets re-added on rebuild only if present.
        fs::remove(v2);
        m.remove(v2.filename().string());
        CHECK_EQ(m.rebuildFromDisk(), std::size_t(0));
        makeVideo(dir, "seg", 2, "recording-two-restored", t);
        CHECK_EQ(m.rebuildFromDisk(), std::size_t(1));
    }

    fs::remove_all(dir);
    TEST_RESULT();
}
