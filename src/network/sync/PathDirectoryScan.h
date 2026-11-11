#pragma once

#include "CooperativeDirectoryScan.h"
#include "HalStorage.h"

namespace wirecheck {
// Path and callback context must remain valid until the scan is destroyed.
// Allocate this object off a small task stack; its cursor retains the path.
class PathDirectoryScan {
 public:
  struct Callbacks {
    void* context;
    bool (*keepGoing)(void*);
    bool (*emit)(void*, const DirectoryEntry&);
  };
  PathDirectoryScan(HalStorage& storage, const HalStorage::MutationSnapshot& snapshot, const char* path,
                    Callbacks callbacks)
      : storage_(storage),
        snapshot_(snapshot),
        path_(path),
        callbacks_(callbacks),
        format_(storage_.snapshotFormat(snapshot_)),
        scan_(format_ == HalStorage::SnapshotFormat::ExFat ? Format::ExFat : Format::Fat,
              {this, readRecord, snapshotValid, keepGoing, emit, close}) {}
  PathDirectoryScan(const PathDirectoryScan&) = delete;
  PathDirectoryScan& operator=(const PathDirectoryScan&) = delete;
  ScanResult step() { return scan_.step(); }
  void cancel() { scan_.cancel(); }

 private:
  static ReadResult readRecord(void* context, uint8_t* record) {
    auto& self = *static_cast<PathDirectoryScan*>(context);
    switch (self.storage_.readDirectoryRecordAtPath(self.path_, self.snapshot_, self.cursor_, record)) {
      case HalStorage::SnapshotRead::Record:
        return ReadResult::Record;
      case HalStorage::SnapshotRead::End:
        return ReadResult::End;
      case HalStorage::SnapshotRead::Changed:
        return ReadResult::Changed;
      case HalStorage::SnapshotRead::Failed:
        return ReadResult::Error;
    }
    return ReadResult::Error;
  }
  static bool snapshotValid(void* context) {
    auto& self = *static_cast<PathDirectoryScan*>(context);
    return self.format_ != HalStorage::SnapshotFormat::Unknown &&
           self.storage_.mutationSnapshotUnchanged(self.snapshot_);
  }
  static bool keepGoing(void* context) {
    auto& self = *static_cast<PathDirectoryScan*>(context);
    return self.callbacks_.keepGoing && self.callbacks_.keepGoing(self.callbacks_.context);
  }
  static bool emit(void* context, const DirectoryEntry& entry) {
    auto& self = *static_cast<PathDirectoryScan*>(context);
    return self.callbacks_.emit && self.callbacks_.emit(self.callbacks_.context, entry);
  }
  // No filesystem handle survives a batch read.
  static bool close(void*) { return true; }
  HalStorage& storage_;
  HalStorage::MutationSnapshot snapshot_;
  const char* path_;
  Callbacks callbacks_;
  HalStorage::SnapshotFormat format_;
  HalStorage::DirectoryCursor cursor_{};
  CooperativeDirectoryScan scan_;
};
}  // namespace wirecheck
