#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include "ExFatReservedStage.h"

namespace wirecheck {
// Single-task owner. Allocate off a small task stack. The callback context and
// cancellation flag must outlive this object; no network input supplies card
// geometry. This owner only supports the current SDK's MBR exFAT mount.
class ExFatStageOwner {
 public:
  using State = ExFatReservedStage::State;
  struct Callbacks {
    void* context;
    bool (*active)(void*);
  };

  ExFatStageOwner(HalStorage& storage, Callbacks callbacks, const std::atomic_flag& cancelled)
      : storage_(storage), callbacks_(callbacks), cancelled_(cancelled) {}
  ExFatStageOwner(const ExFatStageOwner&) = delete;
  ExFatStageOwner& operator=(const ExFatStageOwner&) = delete;

  bool begin() {
    if (state_ != State::Idle || !active() || !storage_.beginMutationSnapshot(snapshot_) ||
        !storage_.snapshotGeometry(snapshot_, geometry_) ||
        storage_.snapshotFormat(snapshot_) != HalStorage::SnapshotFormat::ExFat ||
        !storage_.readExFatSector(snapshot_, HalStorage::ExFatSector::Metadata, 0, sector_))
      return fail();
    if (sector_[510] != 0x55 || sector_[511] != 0xaa || (sector_[446] != 0 && sector_[446] != 0x80) ||
        sector_[450] != 7)
      return fail();
    const uint32_t start = readLe32(sector_ + 454), size = readLe32(sector_ + 458);
    stage_.emplace(storage_, snapshot_, ExFatLayoutScan::Callbacks{this, activeCallback, readFat},
                   FreeSpaceScan::Callbacks{this, activeCallback, readMetadata},
                   FreeSpaceScan::Callbacks{this, activeCallback, readBitmap}, cancelled_);
    if (!stage_->begin({FreeClusterCounter::Format::ExFat, geometry_.dataStart, geometry_.clusterCount,
                        geometry_.clusterBytes, geometry_.deviceSectors},
                       {start, size, geometry_.deviceSectors, geometry_.fatStart, geometry_.dataStart,
                        geometry_.clusterCount, geometry_.clusterBytes}))
      return fail();
    state_ = stage_->state();
    return true;
  }

  State step() {
    if (!stage_ || state_ == State::Failed) return state_;
    state_ = stage_->step();
    return state_;
  }
  bool beginStage(const char* name, uint64_t size, uint64_t reserve) {
    if (state_ != State::Ready || !stage_ || !active() || !stage_->beginStage(name, size, reserve)) return fail();
    state_ = stage_->state();
    return true;
  }
  bool append(const uint8_t* data, size_t count) {
    if (state_ != State::Writing || !stage_ || !active() || !stage_->append(data, count)) return fail();
    return true;
  }
  bool finish() {
    if (state_ != State::Writing || !stage_ || !active() || !stage_->finish()) return fail();
    state_ = stage_->state();
    return true;
  }
  State state() const { return state_; }

 private:
  bool active() const {
    return callbacks_.active && callbacks_.active(callbacks_.context) && !cancelled_.test(std::memory_order_relaxed);
  }
  static bool activeCallback(void* context) { return static_cast<ExFatStageOwner*>(context)->active(); }
  static uint32_t readLe32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
  }
  static bool readFat(void* context, uint32_t cluster, uint32_t& value) {
    auto& self = *static_cast<ExFatStageOwner*>(context);
    if (!self.active() ||
        !self.storage_.readExFatSector(self.snapshot_, HalStorage::ExFatSector::Fat, cluster / 128, self.sector_))
      return false;
    value = readLe32(self.sector_ + (cluster % 128) * 4);
    return true;
  }
  static bool readMetadata(void* context, uint32_t sector, uint8_t* out) {
    auto& self = *static_cast<ExFatStageOwner*>(context);
    return self.active() &&
           self.storage_.readExFatSector(self.snapshot_, HalStorage::ExFatSector::Metadata, sector, out);
  }
  static bool readBitmap(void* context, uint32_t sector, uint8_t* out) {
    auto& self = *static_cast<ExFatStageOwner*>(context);
    return self.active() && sector >= self.geometry_.dataStart &&
           self.storage_.readExFatSector(self.snapshot_, HalStorage::ExFatSector::Bitmap,
                                         sector - self.geometry_.dataStart, out);
  }
  bool fail() {
    state_ = State::Failed;
    return false;
  }
  HalStorage& storage_;
  Callbacks callbacks_;
  const std::atomic_flag& cancelled_;
  HalStorage::MutationSnapshot snapshot_{};
  HalStorage::VolumeGeometry geometry_{};
  std::optional<ExFatReservedStage> stage_;
  uint8_t sector_[512]{};
  State state_ = State::Idle;
};
}  // namespace wirecheck
