#pragma once

#include <cstddef>
#include <cstdint>

namespace statsbackup {
struct RetentionResult {
  uint8_t removed = 0;
  bool deferred = false;
};
// Scans numbered manual or undated snapshots without changing files. The
// latest name is optional; failures leave outputs untouched.
bool nextDeviceBackupSequence(char group, uint32_t& next, char* latest = nullptr, size_t latestSize = 0);
// Called only after verifying the current automatic snapshot. Each recognized group
// retains seven entries independently. Legacy hash-only undated files remain.
RetentionResult pruneAutomaticDeviceBackups(const char* current);
}  // namespace statsbackup
