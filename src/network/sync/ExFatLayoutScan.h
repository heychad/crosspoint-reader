#pragma once

#include <cstdint>

#include "ExFatBitmapLayout.h"
#include "HalStorage.h"

namespace wirecheck {
// One owner steps this scan; its callback context outlives it.
// Geometry and FAT reads must describe the mounted volume, and the callback
// must read through a coherent cache while the HAL storage lock is held.
class ExFatLayoutScan {
 public:
  enum class State { Idle, Records, Chain, Complete, Failed };
  struct Callbacks {
    void* context;
    bool (*active)(void*);
    bool (*readFatEntry)(void*, uint32_t cluster, uint32_t& value);
  };

  ExFatLayoutScan(HalStorage& storage, const HalStorage::MutationSnapshot& snapshot, Callbacks callbacks)
      : storage_(storage), snapshot_(snapshot), callbacks_(callbacks) {}
  ExFatLayoutScan(const ExFatLayoutScan&) = delete;
  ExFatLayoutScan& operator=(const ExFatLayoutScan&) = delete;

  bool begin(uint32_t clusterCount, uint32_t clusterBytes, uint8_t fatCount) {
    if (state_ != State::Idle || !callbacks_.active || !callbacks_.readFatEntry ||
        !callbacks_.active(callbacks_.context) || !storage_.mutationSnapshotUnchanged(snapshot_) ||
        !layout_.begin(clusterCount, clusterBytes, fatCount))
      return fail();
    state_ = State::Records;
    return true;
  }

  State step() {
    if (state_ != State::Records && state_ != State::Chain) return state_;
    if (!callbacks_.active(callbacks_.context) || !storage_.mutationSnapshotUnchanged(snapshot_)) {
      fail();
      return state_;
    }
    if (state_ == State::Records) {
      uint8_t record[32];
      const auto result = storage_.readRootDirectoryRecord(snapshot_, cursor_, record);
      if (result == HalStorage::SnapshotRead::End || (result == HalStorage::SnapshotRead::Record && record[0] == 0)) {
        if (layout_.endRecords())
          state_ = State::Chain;
        else
          fail();
      } else if (result != HalStorage::SnapshotRead::Record || !layout_.record(record, sizeof(record))) {
        fail();
      }
    } else {
      if (!storage_.withStorageLock(readFatUnderLock, this) || !layout_.chainEntry(fatValue_)) {
        fail();
      } else if (layout_.state() == ExFatBitmapLayout::State::Complete) {
        state_ = State::Complete;
      } else {
        ++nextCluster_;
      }
    }
    return state_;
  }

  bool valid() const {
    return state_ == State::Complete && callbacks_.active && callbacks_.active(callbacks_.context) &&
           storage_.mutationSnapshotUnchanged(snapshot_);
  }
  State state() const { return state_; }

 private:
  static bool readFatUnderLock(void* context) {
    auto& self = *static_cast<ExFatLayoutScan*>(context);
    return self.callbacks_.active(self.callbacks_.context) && self.storage_.mutationSnapshotUnchanged(self.snapshot_) &&
           self.callbacks_.readFatEntry(self.callbacks_.context, self.nextCluster_, self.fatValue_) &&
           self.storage_.mutationSnapshotUnchanged(self.snapshot_);
  }
  bool fail() {
    state_ = State::Failed;
    return false;
  }
  HalStorage& storage_;
  HalStorage::MutationSnapshot snapshot_;
  HalStorage::RootDirectoryCursor cursor_;
  Callbacks callbacks_;
  ExFatBitmapLayout layout_;
  uint32_t nextCluster_ = 2, fatValue_ = 0;
  State state_ = State::Idle;
};
}  // namespace wirecheck
