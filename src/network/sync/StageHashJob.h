#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "StageHash.h"

namespace wirecheck {
// Caller owns the job, scratch buffer, request lifetime, and HTTP response.
class StageHashJob {
 public:
  enum class State { Idle, Reading, Ready, Failed };
  struct Callbacks {
    void* context;
    bool (*active)(void*);
    bool (*verifyIdentity)(void*, const char*);
  };

  StageHashJob(HalStorage& storage, uint8_t* buffer, size_t capacity, Callbacks callbacks)
      : hash(storage, buffer, capacity), callbacks(callbacks) {}

  bool begin(const char* stage, uint64_t size, const char* deviceId) {
    if (state_ != State::Idle || !callbacks.active || !callbacks.verifyIdentity || !deviceId ||
        strnlen(deviceId, sizeof(identity)) != 64)
      return fail();
    for (size_t i = 0; i < 64; ++i) {
      const char c = deviceId[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return fail();
    }
    std::memcpy(identity, deviceId, sizeof(identity));
    if (!callbacks.active(callbacks.context) || !callbacks.verifyIdentity(callbacks.context, identity) ||
        !hash.begin(stage, size))
      return fail();
    state_ = State::Reading;
    return true;
  }

  State step() {
    if (state_ != State::Reading) return state_;
    if (!callbacks.active(callbacks.context)) {
      fail();
      return state_;
    }
    switch (hash.step()) {
      case StageHash::State::Reading:
        break;
      case StageHash::State::Complete:
        state_ = State::Ready;
        break;
      case StageHash::State::Idle:
      case StageHash::State::Failed:
        fail();
        break;
    }
    return state_;
  }

  bool copyHex(char out[65]) {
    if (state_ != State::Ready || !out) return false;
    uint8_t digest[32];
    if (!callbacks.active(callbacks.context) || !callbacks.verifyIdentity(callbacks.context, identity) ||
        !hash.copyDigest(digest))
      return fail();
    static constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(digest); ++i) {
      out[i * 2] = hex[digest[i] >> 4];
      out[i * 2 + 1] = hex[digest[i] & 15];
    }
    out[64] = '\0';
    return true;
  }

  void cancel() {
    if (state_ != State::Failed) fail();
  }
  State state() const { return state_; }

 private:
  bool fail() {
    hash.cancel();
    state_ = State::Failed;
    return false;
  }

  StageHash hash;
  Callbacks callbacks;
  char identity[65]{};
  State state_ = State::Idle;
};
}  // namespace wirecheck
