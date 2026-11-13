#pragma once

#include <cstddef>

namespace movereference {
// Replaces the supported state or Recents file without deleting backups.
// Any uncertain I/O leaves recovery artifacts for inspection.
// finishPublishedOnly never publishes a new primary; it can archive a leftover
// backup only when the existing primary exactly matches the expected bytes.
bool write(const char* path, const char* bytes, size_t size, bool finishPublishedOnly = false);
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
bool writeSleepPreferences(const char* bytes, size_t size);
// Preserves each interrupted sleep-write artifact in a numbered archive.
// Caller holds the recursive storage lock and excludes active completion
// transactions through recovery and the following state save.
bool recoverSleepPreferences(bool (*isValidState)(const char*));
#endif
}  // namespace movereference
