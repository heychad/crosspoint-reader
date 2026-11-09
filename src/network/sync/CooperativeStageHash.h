#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "StageHashJob.h"

namespace wirecheck {
// One owner calls every method; the buffer and callback context outlive this job.
// A call to step hashes at most one buffer. The owner schedules it between services.
class CooperativeStageHash {
 public:
  enum class State { Idle, Reading, Ready, Failed };
  CooperativeStageHash(HalStorage& storage, uint8_t* buffer, size_t capacity, StageHashJob::Callbacks callbacks)
      : job(storage, buffer, capacity, callbacks) {}
  CooperativeStageHash(const CooperativeStageHash&) = delete;
  CooperativeStageHash& operator=(const CooperativeStageHash&) = delete;

  bool begin(const char* stage, uint64_t size, const char* identity, uint32_t now) {
    if (state_ != State::Idle || !stage || !identity || strnlen(stage, sizeof(name)) != sizeof(name) - 1 ||
        strnlen(identity, sizeof(device)) != sizeof(device) - 1 || !job.begin(stage, size, identity))
      return fail();
    std::memcpy(name, stage, sizeof(name));
    std::memcpy(device, identity, sizeof(device));
    expectedSize = size;
    started = now;
    bound = true;
    state_ = State::Reading;
    return true;
  }
  State step(uint32_t now) {
    if (state_ == State::Failed || state_ == State::Idle) return state_;
    if (now - started >= MAX_AGE_MS) {
      fail();
      return state_;
    }
    if (state_ == State::Reading) {
      const auto next = job.step();
      if (next == StageHashJob::State::Ready) state_ = State::Ready;
      if (next == StageHashJob::State::Failed || next == StageHashJob::State::Idle) fail();
    }
    return state_;
  }
  bool matches(const char* stage, uint64_t size, const char* identity) const {
    return bound && stage && identity && size == expectedSize && std::strcmp(stage, name) == 0 &&
           std::strcmp(identity, device) == 0;
  }
  bool copyHex(const char* stage, uint64_t size, const char* identity, uint32_t now, char out[65]) {
    if (!matches(stage, size, identity) || state_ != State::Ready || step(now) != State::Ready) return false;
    if (!job.copyHex(out)) return fail();
    return true;
  }
  void cancel() { fail(); }
  State state() const { return state_; }

 private:
  static constexpr uint32_t MAX_AGE_MS = 60U * 60U * 1000U;
  bool fail() {
    job.cancel();
    state_ = State::Failed;
    return false;
  }
  StageHashJob job;
  char name[46]{};
  char device[65]{};
  uint64_t expectedSize = 0;
  uint32_t started = 0;
  bool bound = false;
  State state_ = State::Idle;
};
}  // namespace wirecheck
