#pragma once

#include <cstddef>
#include <cstdint>

#include "FreeClusterCounter.h"
#include "HalStorage.h"

namespace wirecheck {
class ReservedStage;
// The owner provides geometry from the mounted volume and a sector reader
// that is safe under the HAL lock. Keep this object off a small task stack.
class FreeSpaceScan {
 public:
  enum class State { Idle, Reading, Complete, Failed };
  struct Geometry {
    FreeClusterCounter::Format format;
    uint32_t tableStart;
    uint32_t clusterCount;
    uint32_t clusterBytes;
    uint32_t deviceSectors;
  };
  struct Callbacks {
    void* context;
    bool (*active)(void*);
    bool (*readSector)(void*, uint32_t, uint8_t*);
  };

  FreeSpaceScan(HalStorage& storage, const HalStorage::MutationSnapshot& snapshot, Callbacks callbacks)
      : storage(storage), snapshot(snapshot), callbacks(callbacks) {}
  FreeSpaceScan(const FreeSpaceScan&) = delete;
  FreeSpaceScan& operator=(const FreeSpaceScan&) = delete;

  bool begin(const Geometry& geometry) {
    if (state_ != State::Idle || !callbacks.active || !callbacks.readSector || !callbacks.active(callbacks.context) ||
        !storage.mutationSnapshotUnchanged(snapshot) ||
        !counter.begin(geometry.format, geometry.clusterCount, geometry.clusterBytes))
      return fail();
    const uint64_t entries =
        static_cast<uint64_t>(geometry.clusterCount) + (geometry.format == FreeClusterCounter::Format::ExFat ? 0 : 2);
    const uint64_t perSector = geometry.format == FreeClusterCounter::Format::ExFat   ? 4096
                               : geometry.format == FreeClusterCounter::Format::Fat16 ? 256
                                                                                      : 128;
    const uint64_t count = (entries + perSector - 1) / perSector;
    if (!count || static_cast<uint64_t>(geometry.tableStart) + count > geometry.deviceSectors) return fail();
    nextSector = geometry.tableStart;
    state_ = State::Reading;
    return true;
  }

  State step() {
    if (state_ != State::Reading) return state_;
    if (!callbacks.active(callbacks.context) || !storage.withStorageLock(readUnderLock, this)) {
      fail();
      return state_;
    }
    ++nextSector;
    const auto result = counter.consume(sector, sizeof(sector));
    if (result == FreeClusterCounter::State::Complete) state_ = State::Complete;
    if (result == FreeClusterCounter::State::Failed) fail();
    return state_;
  }

  // This is provisional. Admission must recheck the same snapshot and create
  // the stage in a single storage-locked transaction.
  bool allowsStage(uint64_t size, uint64_t reserve) { return active() && allowsStageUnderLock(size, reserve); }
  bool active() const { return callbacks.active && callbacks.active(callbacks.context); }
  bool freeBytes(uint64_t& out) {
    return state_ == State::Complete && callbacks.active(callbacks.context) &&
           storage.mutationSnapshotUnchanged(snapshot) && counter.freeBytes(out);
  }
  void cancel() { fail(); }
  State state() const { return state_; }

 private:
  friend class ReservedStage;
  // Caller must hold the same storage lock through creation of the stage.
  bool allowsStageUnderLock(uint64_t size, uint64_t reserve) {
    return state_ == State::Complete && storage.mutationSnapshotUnchanged(snapshot) &&
           counter.allowsStage(size, reserve);
  }
  static bool readUnderLock(void* context) {
    auto& self = *static_cast<FreeSpaceScan*>(context);
    return self.storage.mutationSnapshotUnchanged(self.snapshot) &&
           self.callbacks.readSector(self.callbacks.context, self.nextSector, self.sector) &&
           self.storage.mutationSnapshotUnchanged(self.snapshot);
  }
  bool fail() {
    state_ = State::Failed;
    return false;
  }

  HalStorage& storage;
  HalStorage::MutationSnapshot snapshot;
  Callbacks callbacks;
  FreeClusterCounter counter;
  uint8_t sector[512]{};
  uint32_t nextSector = 0;
  State state_ = State::Idle;
};
}  // namespace wirecheck
