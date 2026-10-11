#include "cleanup.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <set>

namespace fs = std::filesystem;
namespace ch = std::chrono;

namespace {

// File age from its modification time, in the file clock's own domain so no
// cross-clock epoch conversion (or timezone parsing of the manifest timestamp)
// is needed. A stat failure yields a negative age, which the policy treats as
// "unknown" and protects.
std::chrono::milliseconds ageOf(const fs::path& path) {
    std::error_code ec;
    auto mtime = fs::last_write_time(path, ec);
    if (ec) {
        return std::chrono::milliseconds(-1);
    }
    using FileClock = fs::file_time_type::clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(FileClock::now() - mtime);
}

} // namespace

namespace cleanup {

std::vector<std::string> selectVictims(const std::vector<Candidate>& files,
                                       std::uintmax_t freeBytes,
                                       const Policy& policy) {
    std::vector<Candidate> sorted = files;
    std::sort(sorted.begin(), sorted.end(), [](const Candidate& a, const Candidate& b) {
        if (a.age != b.age) {
            return a.age > b.age; // Oldest (largest age) first.
        }
        return a.file < b.file; // Deterministic tie-break.
    });

    std::set<std::string> selected;
    std::uintmax_t freed = 0;

    // Pass 1 — age retention: every file past the limit (and past grace).
    if (policy.maxAge.count() > 0) {
        for (const Candidate& c : sorted) {
            if (c.age >= policy.grace && c.age >= policy.maxAge) {
                selected.insert(c.file);
                freed += c.sizeBytes;
            }
        }
    }

    // Pass 2 — free-space floor: keep deleting the oldest remaining file,
    // honouring the grace protection, until the target is met or nothing more
    // is eligible (we then stop rather than reclaim a still-fresh file).
    for (const Candidate& c : sorted) {
        if (freeBytes + freed >= policy.minFreeBytes) {
            break;
        }
        if (selected.count(c.file) || c.age < policy.grace) {
            continue;
        }
        selected.insert(c.file);
        freed += c.sizeBytes;
    }

    std::vector<std::string> out;
    for (const Candidate& c : sorted) {
        if (selected.count(c.file)) {
            out.push_back(c.file);
        }
    }
    return out;
}

Result enforce(Manifest& manifest, const fs::path& videoDir, const Policy& policy) {
    Result result;
    if (!policy.enabled) {
        return result;
    }

    std::error_code ec;
    auto space = fs::space(videoDir, ec);
    // If the volume cannot be stat'd, assume there is plenty of room rather
    // than risk deleting recordings on a transient error.
    std::uintmax_t freeBytes = ec ? std::numeric_limits<std::uintmax_t>::max() : space.available;

    std::vector<Candidate> candidates;
    for (const ManifestEntry& e : manifest.entries()) {
        fs::path file = videoDir / e.file;
        if (!fs::is_regular_file(file, ec)) {
            continue; // Registered but already gone; leave the entry alone.
        }
        std::error_code sec;
        if (fs::exists(fs::path(file).concat(".writing"), sec)) {
            continue; // Open segment: never a victim.
        }
        Candidate c;
        c.file = e.file;
        std::error_code sizEc;
        auto size = fs::file_size(file, sizEc);
        c.sizeBytes = sizEc ? e.sizeBytes : size;
        c.age = ageOf(file);
        candidates.push_back(std::move(c));
    }

    for (const std::string& name : selectVictims(candidates, freeBytes, policy)) {
        fs::path file = videoDir / name;
        std::error_code dec;
        auto size = fs::file_size(file, dec);
        std::uintmax_t reclaimed = dec ? 0 : size;
        fs::remove(file, dec);
        if (dec) {
            std::cerr << "Cleanup: could not delete " << file << ": " << dec.message() << std::endl;
            continue;
        }
        manifest.remove(name);
        result.deleted++;
        result.freedBytes += reclaimed;
    }

    if (result.deleted > 0 && !manifest.save()) {
        std::cerr << "Cleanup: failed to persist manifest" << std::endl;
    }

    auto after = fs::space(videoDir, ec);
    result.spaceOk = ec ? true : after.available >= policy.minFreeBytes;
    return result;
}

} // namespace cleanup
