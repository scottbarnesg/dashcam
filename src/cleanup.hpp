#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "manifest.hpp"

#ifndef CLEANUP_H
#define CLEANUP_H

// Old-file cleanup (BACKLOG item 5b). Bounds local storage so the SD card
// never fills, by deleting the oldest manifest-registered recordings until the
// configured free-space floor and/or age retention limit are satisfied.
//
// It operates only on files the manifest knows about and that are present on
// disk, so it can never touch the in-progress segment (registered only once
// closed) or a quarantined file (never registered). As a belt-and-braces guard
// any candidate still carrying a ".writing" sentinel is skipped too.
//
// Deletion is oldest-first. Once server offload (item 5c) lands, already
// offloaded files become the preferred victims; until then every recording is
// Local and age/space alone decides, which is the correct loop-recording
// behaviour for a device with no server.
namespace cleanup {

struct Policy {
    bool enabled = true;
    // Keep at least this many bytes free after a cleanup pass. 0 disables the
    // free-space rule (age-only retention).
    std::uintmax_t minFreeBytes = 0;
    // Delete any recording older than this. Zero (the default) disables the
    // age rule so that space is the only driver (loop recording).
    std::chrono::milliseconds maxAge{0};
    // Never reclaim a file younger than this, even under a space crunch: it may
    // still be finalising or being read. Also protects files whose age cannot
    // be determined.
    std::chrono::milliseconds grace{std::chrono::minutes(15)};
};

struct Result {
    std::size_t deleted = 0;
    std::uintmax_t freedBytes = 0;
    bool spaceOk = true; // Free-space floor satisfied after the pass.
};

// A deletion candidate for the pure selector: a file's identity, its size, and
// its age. Age is measured by the caller (enforce uses the file's mtime); the
// selector treats a negative age as "unknown" and never selects it.
struct Candidate {
    std::string file;
    std::uintmax_t sizeBytes = 0;
    std::chrono::milliseconds age{0};
};

// Pure victim selection (no filesystem access, no side effects): the file
// names to delete, oldest-first, to satisfy the policy given the currently
// available bytes. Exposed for unit testing of the budgeting rules.
std::vector<std::string> selectVictims(const std::vector<Candidate>& files,
                                       std::uintmax_t freeBytes,
                                       const Policy& policy);

// Reclaim space to satisfy the policy: delete victims from disk, drop them
// from the manifest, and persist the manifest atomically if anything changed.
// The in-progress/quarantined files are never touched. Returns what was freed.
Result enforce(Manifest& manifest,
               const std::filesystem::path& videoDir,
               const Policy& policy);

} // namespace cleanup

#endif
