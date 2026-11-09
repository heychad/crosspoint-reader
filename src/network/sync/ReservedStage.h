#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "ExclusiveStage.h"
#include "FreeSpaceScan.h"

namespace wirecheck {
// The owner allocates this object off a small task stack and supplies checked
// mounted-volume geometry and a sector reader.
// Scan and stage share one snapshot and one storage lock at admission.
// One task owns all methods; the flag and callback context outlive this object.
// Other tasks set the flag and wait for teardown. An in-flight operation may finish.
class ReservedStage {
 public:
  enum class State { Idle, Scanning, Ready, Writing, Complete, Failed };

  ReservedStage(HalStorage& storage, const HalStorage::MutationSnapshot& snapshot, FreeSpaceScan::Callbacks callbacks,
                const std::atomic_flag& cancelled)
      : storage(storage), scan(storage, snapshot, callbacks), stage(storage), cancelled(cancelled) {}
  ReservedStage(const ReservedStage&) = delete;
  ReservedStage& operator=(const ReservedStage&) = delete;

  bool beginScan(const FreeSpaceScan::Geometry& geometry) {
    if (state_ != State::Idle || cancelled.test(std::memory_order_relaxed) || !scan.begin(geometry)) return fail();
    state_ = State::Scanning;
    return true;
  }
  State stepScan() {
    if (state_ != State::Scanning) return state_;
    if (cancelled.test(std::memory_order_relaxed)) {
      fail();
      return state_;
    }
    const auto result = scan.step();
    if (result == FreeSpaceScan::State::Complete) state_ = State::Ready;
    if (result == FreeSpaceScan::State::Failed) fail();
    return state_;
  }
  bool beginStage(const char* name, uint64_t size, uint64_t reserve) {
    if (state_ != State::Ready || cancelled.test(std::memory_order_relaxed) || !scan.active()) return fail();
    Admission request{this, name, size, reserve};
    if (!storage.withStorageLock(admitUnderLock, &request)) return fail();
    state_ = State::Writing;
    return true;
  }
  bool append(const uint8_t* data, size_t count) {
    if (state_ != State::Writing || cancelled.test(std::memory_order_relaxed)) return fail();
    Write request{this, data, count};
    if (!storage.withStorageLock(writeUnderLock, &request)) return fail();
    return true;
  }
  bool finish() {
    if (state_ != State::Writing || cancelled.test(std::memory_order_relaxed) ||
        !storage.withStorageLock(finishUnderLock, this))
      return fail();
    state_ = State::Complete;
    return true;
  }
  void cancel() {
    if (state_ != State::Complete) fail();
  }
  State state() const { return state_; }

 private:
  struct Admission {
    ReservedStage* self;
    const char* name;
    uint64_t size;
    uint64_t reserve;
  };
  struct Write {
    ReservedStage* self;
    const uint8_t* data;
    size_t count;
  };
  static bool admitUnderLock(void* context) {
    const auto& request = *static_cast<Admission*>(context);
    return !request.self->cancelled.test(std::memory_order_relaxed) &&
           request.self->scan.allowsStageUnderLock(request.size, request.reserve) &&
           request.self->stage.begin(request.name, request.size);
  }
  static bool writeUnderLock(void* context) {
    const auto& request = *static_cast<Write*>(context);
    return !request.self->cancelled.test(std::memory_order_relaxed) &&
           request.self->stage.append(request.data, request.count);
  }
  static bool finishUnderLock(void* context) {
    auto& self = *static_cast<ReservedStage*>(context);
    return !self.cancelled.test(std::memory_order_relaxed) && self.stage.finish();
  }
  bool fail() {
    scan.cancel();
    state_ = State::Failed;
    return false;
  }

  HalStorage& storage;
  FreeSpaceScan scan;
  ExclusiveStage stage;
  const std::atomic_flag& cancelled;
  State state_ = State::Idle;
};
}  // namespace wirecheck
