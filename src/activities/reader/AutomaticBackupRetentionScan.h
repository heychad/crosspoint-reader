#pragma once

#include "AutomaticBackupRetention.h"
#include "network/sync/PathDirectoryScan.h"

namespace statsbackup {
// Read-only, cooperative scan. Keep this object on the heap and keep the
// snapshot alive for the entire scan and candidate inspection. A candidate
// is provisional: no file integrity, ownership or deletion is established.
class AutomaticBackupRetentionScan {
 public:
  AutomaticBackupRetentionScan(HalStorage& storage, const HalStorage::MutationSnapshot& snapshot, const char* current)
      : storage_(storage),
        snapshot_(snapshot),
        planner_(current),
        scan_(storage, snapshot, DIRECTORY, {this, keepGoing, observe}) {}
  AutomaticBackupRetentionScan(const AutomaticBackupRetentionScan&) = delete;
  AutomaticBackupRetentionScan& operator=(const AutomaticBackupRetentionScan&) = delete;

  wirecheck::ScanResult step() {
    result_ = scan_.step();
    return result_;
  }
  void cancel() {
    scan_.cancel();
    result_ = wirecheck::ScanResult::Failed;
  }
  const char* candidate() {
    if (result_ != wirecheck::ScanResult::Complete || !storage_.mutationSnapshotUnchanged(snapshot_)) return nullptr;
    return planner_.candidate(true);
  }
  uint32_t count() const { return planner_.count(); }

 private:
  static constexpr char DIRECTORY[] = "/.crossink-stats-backup";
  static bool keepGoing(void*) { return true; }
  static bool observe(void* raw, const wirecheck::DirectoryEntry& entry) {
    if (!entry.directory) static_cast<AutomaticBackupRetentionScan*>(raw)->planner_.observe(entry.name);
    return true;
  }
  HalStorage& storage_;
  HalStorage::MutationSnapshot snapshot_;
  AutomaticBackupRetention planner_;
  wirecheck::PathDirectoryScan scan_;
  wirecheck::ScanResult result_ = wirecheck::ScanResult::Pending;
};
}  // namespace statsbackup
