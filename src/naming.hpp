#include <chrono>
#include <string>

#ifndef NAMING_H
#define NAMING_H

// Collision-free, sortable recording file names (BACKLOG item 5a):
//   dashcam_<context>_YYYYMMDD-HHMMSS_NNNN.mp4
// NNNN is a zero-padded per-session sequence number: unique even when the
// clock is wrong (offline cold boot) or two recordings start in the same
// second, and lexicographic sort equals chronological sort within a session.
namespace naming {

// Local-time timestamp used in names, e.g. 20261007-183005.
std::string timestamp(std::chrono::system_clock::time_point startTime);

// Full file name for a recording. context is e.g. "seg" (driving segment).
std::string fileName(std::chrono::system_clock::time_point startTime,
                     const std::string& context,
                     unsigned long sequence);

// True if the name matches the pattern above (used to filter video dir).
bool matches(const std::string& name);

} // namespace naming

#endif
