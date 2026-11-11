#pragma once

#include <cstdint>

namespace statsbackup {
struct RetentionResult {
  uint8_t removed = 0;
  bool deferred = false;
};
// Called only after verifying the current automatic snapshot. Dated snapshots
// retain seven entries. Manual/undated files are protected, never pruned here.
RetentionResult pruneAutomaticDeviceBackups(const char* current);
}  // namespace statsbackup
