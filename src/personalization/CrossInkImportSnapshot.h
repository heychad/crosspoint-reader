#pragma once

#include <cstddef>

namespace crossinkimport {
inline constexpr size_t SNAPSHOT_PATH_SIZE = 64;
// Preserve an exact copy of the checked current primary in a new file.
// The caller must retain its settings/store lock across snapshot and save.
bool snapshotCurrentSettings(const char* expected, size_t size, char (&path)[SNAPSHOT_PATH_SIZE]);
}  // namespace crossinkimport
