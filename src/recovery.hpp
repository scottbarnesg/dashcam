#include <filesystem>

#ifndef RECOVERY_H
#define RECOVERY_H

// Boot recovery (BACKLOG item 3). A segment file with a lingering
// "<name>.mp4.writing" sentinel was open when power was lost: it is not
// registered in the manifest and may be corrupt. Move such files to
// <videoDir>/quarantine/ (kept for forensics; cleanup/offload never touch
// them) so recording can continue. Returns the number of files quarantined.
std::size_t quarantineIncompleteSegments(const std::filesystem::path& videoDir);

#endif
