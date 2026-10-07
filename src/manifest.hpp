#include <chrono>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#ifndef MANIFEST_H
#define MANIFEST_H

// Recording manifest (BACKLOG item 5a). The single source of truth that
// cleanup (5b) and offload (5c) consume. Persisted as one JSON object per
// line (append-friendly, trivially diffable) written atomically via
// temp-file + rename so a power cut can never leave a half-written manifest.

enum class UploadState {
    Local,           // Recorded, not yet uploaded.
    UploadPending,   // Selected for upload.
    OffloadConfirmed // Server confirmed; safe for 5b to delete.
};

std::string stateToString(UploadState state);
UploadState stateFromString(const std::string& s);

struct ManifestEntry {
    std::string file;            // file name (not path) inside videoDir
    std::string context;         // e.g. "seg", "event"
    std::string startedAt;       // ISO-ish local timestamp, sortable
    double durationSeconds = 0;
    std::uintmax_t sizeBytes = 0;
    std::string sha256;          // lowercase hex of the complete file
    UploadState state = UploadState::Local;
    std::string updated;         // timestamp of last state change
};

class Manifest {
    public:
        explicit Manifest(std::filesystem::path videoDir);

        // Compute hash/size/duration and insert an entry with state Local.
        void addRecording(const std::filesystem::path& videoDirFile,
                          const std::string& context,
                          const std::string& startedAt,
                          double durationSeconds);

        void setState(const std::string& file, UploadState state);
        bool contains(const std::string& file) const;
        bool remove(const std::string& file); // Only for files deleted from disk.
        std::vector<ManifestEntry> entries() const; // Sorted by startedAt, file.
        const ManifestEntry* find(const std::string& file) const;
        std::size_t size() const { return records.size(); }

        bool load();     // Parse manifest.jsonl; corrupt trailing line tolerated.
        bool save() const; // Atomic rewrite of manifest.jsonl.

        // Add entries for any matching recording file on disk missing from the
        // manifest (crash/boot recovery). Existing entries are left untouched.
        std::size_t rebuildFromDisk();

    private:
        std::filesystem::path path; // <videoDir>/manifest.jsonl
        std::map<std::string, ManifestEntry> records;
};

// SHA-256 of a file, lowercase hex. Empty string on I/O error.
std::string sha256File(const std::filesystem::path& path);

#endif
