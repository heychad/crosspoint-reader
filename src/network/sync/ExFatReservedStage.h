#pragma once

#include <atomic>

#include "ExFatBootCheck.h"
#include "ExFatLayoutScan.h"
#include "ReservedStage.h"

namespace wirecheck {
// The caller supplies mounted geometry and cache-coherent callbacks, and keeps
// their context alive. Keep this object off a small task stack; it contains
// sector scratch space.
class ExFatReservedStage {
 public:
  enum class State { Idle, Partition, Boot, Layout, Scanning, Ready, Writing, Complete, Failed };

  ExFatReservedStage(HalStorage& storage, const HalStorage::MutationSnapshot& snapshot,
                     ExFatLayoutScan::Callbacks layoutCallbacks, FreeSpaceScan::Callbacks bootCallbacks,
                     FreeSpaceScan::Callbacks spaceCallbacks, const std::atomic_flag& cancelled)
      : storage_(storage),
        snapshot_(snapshot),
        bootCallbacks_(bootCallbacks),
        spaceCallbacks_(spaceCallbacks),
        layout_(storage, snapshot, layoutCallbacks),
        stage_(storage, snapshot, {this, spaceActive, readSpace}, cancelled),
        cancelled_(cancelled) {}
  ExFatReservedStage(const ExFatReservedStage&) = delete;
  ExFatReservedStage& operator=(const ExFatReservedStage&) = delete;

  bool begin(const FreeSpaceScan::Geometry& geometry, const ExFatBootCheck::Expected& expected) {
    if (state_ != State::Idle || cancelled_.test(std::memory_order_relaxed) ||
        geometry.format != FreeClusterCounter::Format::ExFat || !bootCallbacks_.active || !bootCallbacks_.readSector ||
        !bootCallbacks_.active(bootCallbacks_.context) || !storage_.mutationSnapshotUnchanged(snapshot_) ||
        geometry.deviceSectors != expected.deviceSectors || geometry.tableStart != expected.heapStart ||
        geometry.clusterCount != expected.clusterCount || geometry.clusterBytes != expected.clusterBytes)
      return fail();
    geometry_ = geometry;
    expected_ = expected;
    bootSector_ = 0;
    state_ = State::Partition;
    return true;
  }

  State step() {
    if (state_ != State::Partition && state_ != State::Boot && state_ != State::Layout && state_ != State::Scanning)
      return state_;
    if (cancelled_.test(std::memory_order_relaxed)) {
      fail();
      return state_;
    }
    if (state_ == State::Partition) {
      if (!storage_.withStorageLock(readBootUnderLock, this) || !partitionMatches() || !boot_.begin(expected_)) {
        fail();
      } else {
        bootSector_ = expected_.startSector;
        state_ = State::Boot;
      }
    } else if (state_ == State::Boot) {
      if (!storage_.withStorageLock(readBootUnderLock, this) || !boot_.consume(bootBuffer_, sizeof(bootBuffer_))) {
        fail();
      } else if (boot_.state() == ExFatBootCheck::State::Complete) {
        if (!layout_.begin(geometry_.clusterCount, geometry_.clusterBytes, 1))
          fail();
        else
          state_ = State::Layout;
      } else {
        ++bootSector_;
      }
    } else if (state_ == State::Layout) {
      const auto result = layout_.step();
      if (result == ExFatLayoutScan::State::Failed) fail();
      if (result == ExFatLayoutScan::State::Complete) {
        if (!layout_.valid() || !stage_.beginScan(geometry_))
          fail();
        else
          state_ = State::Scanning;
      }
    } else {
      const auto result = stage_.stepScan();
      if (result == ReservedStage::State::Failed)
        fail();
      else if (result == ReservedStage::State::Ready)
        state_ = State::Ready;
    }
    return state_;
  }

  bool beginStage(const char* name, uint64_t size, uint64_t reserve) {
    if (state_ != State::Ready || !layout_.valid() || !stage_.beginStage(name, size, reserve)) return fail();
    state_ = State::Writing;
    return true;
  }
  bool append(const uint8_t* data, size_t count) {
    if (state_ != State::Writing || !stage_.append(data, count)) return fail();
    return true;
  }
  bool finish() {
    if (state_ != State::Writing || !stage_.finish()) return fail();
    state_ = State::Complete;
    return true;
  }
  State state() const { return state_; }

 private:
  static uint32_t readLe32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
  }
  static bool spaceActive(void* context) {
    const auto& self = *static_cast<ExFatReservedStage*>(context);
    return self.spaceCallbacks_.active && self.spaceCallbacks_.active(self.spaceCallbacks_.context);
  }
  static bool readSpace(void* context, uint32_t sector, uint8_t* data) {
    auto& self = *static_cast<ExFatReservedStage*>(context);
    if (!self.spaceCallbacks_.readSector || sector < self.geometry_.tableStart ||
        !self.spaceCallbacks_.readSector(self.spaceCallbacks_.context, sector, data))
      return false;
    // The verified bitmap begins at cluster 2 and must mark all its own
    // contiguous clusters allocated before their free bits can be trusted.
    const uint64_t bytes = (uint64_t(self.geometry_.clusterCount) + 7) / 8;
    const uint64_t bitmapClusters = (bytes + self.geometry_.clusterBytes - 1) / self.geometry_.clusterBytes;
    const uint64_t firstBit = uint64_t(sector - self.geometry_.tableStart) * 4096;
    for (uint64_t bit = firstBit; bit < bitmapClusters && bit < firstBit + 4096; ++bit) {
      const auto local = bit - firstBit;
      if (!(data[local / 8] & (uint8_t(1) << (local % 8)))) return false;
    }
    return true;
  }
  bool partitionMatches() const {
    // The current SDK mounts MBR partition 1. Reject other layouts until the
    // mounted partition identity is available from the HAL.
    return bootBuffer_[510] == 0x55 && bootBuffer_[511] == 0xaa &&
           (bootBuffer_[446] == 0 || bootBuffer_[446] == 0x80) && bootBuffer_[450] == 7 &&
           readLe32(bootBuffer_ + 454) == expected_.startSector &&
           readLe32(bootBuffer_ + 458) == expected_.partitionSectors;
  }
  static bool readBootUnderLock(void* context) {
    auto& self = *static_cast<ExFatReservedStage*>(context);
    return self.bootCallbacks_.active(self.bootCallbacks_.context) &&
           self.storage_.mutationSnapshotUnchanged(self.snapshot_) &&
           self.bootCallbacks_.readSector(self.bootCallbacks_.context, self.bootSector_, self.bootBuffer_) &&
           self.storage_.mutationSnapshotUnchanged(self.snapshot_);
  }
  bool fail() {
    stage_.cancel();
    state_ = State::Failed;
    return false;
  }
  HalStorage& storage_;
  HalStorage::MutationSnapshot snapshot_;
  FreeSpaceScan::Callbacks bootCallbacks_;
  FreeSpaceScan::Callbacks spaceCallbacks_;
  ExFatBootCheck boot_;
  ExFatLayoutScan layout_;
  ReservedStage stage_;
  const std::atomic_flag& cancelled_;
  FreeSpaceScan::Geometry geometry_{};
  ExFatBootCheck::Expected expected_{};
  uint8_t bootBuffer_[512]{};
  uint32_t bootSector_ = 0;
  State state_ = State::Idle;
};
}  // namespace wirecheck
