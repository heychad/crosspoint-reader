#pragma once

#include <utility>

#include "CooperativeDirectoryScan.h"
#include "HalStorage.h"

namespace wirecheck {
// The caller owns this object and its callback context. Acquire the snapshot
// before opening the directory. The HAL supplies the mounted volume format.
// Entries remain provisional until the entire inventory is validated. This
// object contains directory scratch and must not live on a small task stack.
class StorageDirectoryScan {
 public:
  struct Callbacks {
    void* context;
    bool (*keepGoing)(void*);
    bool (*emit)(void*, const DirectoryEntry&);
  };

  StorageDirectoryScan(HalStorage& storage, const HalStorage::MutationSnapshot& snapshot, HalFile&& directory,
                       Callbacks callbacks)
      : storage_(storage),
        snapshot_(snapshot),
        directory_(std::move(directory)),
        callbacks_(callbacks),
        format_(storage_.snapshotFormat(snapshot_)),
        scan_(format_ == HalStorage::SnapshotFormat::ExFat ? Format::ExFat : Format::Fat,
              {this, readRecord, snapshotValid, keepGoing, emit, close}) {}
  StorageDirectoryScan(const StorageDirectoryScan&) = delete;
  StorageDirectoryScan& operator=(const StorageDirectoryScan&) = delete;

  ScanResult step() { return scan_.step(); }
  void cancel() { scan_.cancel(); }

 private:
  static ReadResult readRecord(void* context, uint8_t* record) {
    auto& self = *static_cast<StorageDirectoryScan*>(context);
    switch (self.storage_.readDirectoryRecord(self.directory_, self.snapshot_, record)) {
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
    auto& self = *static_cast<StorageDirectoryScan*>(context);
    return self.format_ != HalStorage::SnapshotFormat::Unknown &&
           self.storage_.mutationSnapshotUnchanged(self.snapshot_);
  }
  static bool keepGoing(void* context) {
    auto& self = *static_cast<StorageDirectoryScan*>(context);
    return self.callbacks_.keepGoing && self.callbacks_.keepGoing(self.callbacks_.context);
  }
  static bool emit(void* context, const DirectoryEntry& entry) {
    auto& self = *static_cast<StorageDirectoryScan*>(context);
    return self.callbacks_.emit && self.callbacks_.emit(self.callbacks_.context, entry);
  }
  static bool close(void* context) {
    auto& self = *static_cast<StorageDirectoryScan*>(context);
    return self.directory_ && self.directory_.close();
  }

  HalStorage& storage_;
  HalStorage::MutationSnapshot snapshot_;
  HalFile directory_;
  Callbacks callbacks_;
  HalStorage::SnapshotFormat format_;
  CooperativeDirectoryScan scan_;
};
}  // namespace wirecheck
