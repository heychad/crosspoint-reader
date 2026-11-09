#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ExFatStageOwner.h"
#include "RawStageRequest.h"

namespace wirecheck {
// The owner retains unread input while Preparing. This object and its supplied
// body buffer live off a small task stack. Deadlines and socket ownership are
// the caller's responsibility. The callback context and cancellation flag
// outlive this object.
class GuardedRawStageSession {
 public:
  enum class State { Headers, Preparing, Body, Complete, Failed };
  struct Callbacks {
    void* context;
    bool (*active)(void*);
    bool (*verifyIdentity)(void*, const char*);
  };

  GuardedRawStageSession(HalStorage& storage, uint8_t* buffer, size_t capacity, uint64_t reserve, Callbacks callbacks,
                         const std::atomic_flag& cancelled)
      : owner_(storage, {this, activeCallback}, cancelled),
        buffer_(buffer),
        capacity_(capacity),
        reserve_(reserve),
        callbacks_(callbacks),
        cancelled_(cancelled) {
    if (!buffer_ || !capacity_ || !callbacks_.active || !callbacks_.verifyIdentity) state_ = State::Failed;
  }
  GuardedRawStageSession(const GuardedRawStageSession&) = delete;
  GuardedRawStageSession& operator=(const GuardedRawStageSession&) = delete;

  // Returns consumed bytes. Input after the headers remains with the caller
  // until preparation finishes; input beyond Content-Length is not consumed.
  size_t consume(const uint8_t* data, size_t length) {
    if (state_ == State::Complete || state_ == State::Failed) return 0;
    if ((!data && length) || !active()) {
      fail();
      return 0;
    }
    if (state_ == State::Preparing) return 0;
    size_t consumed = 0;
    while (consumed < length && state_ == State::Headers) {
      const auto result = request_.consume(data[consumed++]);
      if (result == RawStageRequest::Result::Rejected) {
        fail();
        return consumed;
      }
      if (result == RawStageRequest::Result::Complete) {
        if (!active() || !identityMatches() || !owner_.begin()) {
          fail();
          return consumed;
        }
        state_ = State::Preparing;
      }
    }
    while (consumed < length && state_ == State::Body) {
      if (!active()) {
        fail();
        break;
      }
      const uint64_t remaining = request_.metadata().size() - received_;
      const size_t count = std::min({length - consumed, capacity_ - buffered_, static_cast<size_t>(remaining)});
      std::memcpy(buffer_ + buffered_, data + consumed, count);
      consumed += count;
      buffered_ += count;
      received_ += count;
      if (buffered_ == capacity_ && !flush()) break;
      if (received_ == request_.metadata().size()) finish();
    }
    return consumed;
  }

  // Performs at most one bounded card-scan step per call.
  State step() {
    if (state_ == State::Complete || state_ == State::Failed) return state_;
    if (!active()) {
      fail();
      return state_;
    }
    if (state_ != State::Preparing) return state_;
    const auto state = owner_.step();
    if (state == ExFatStageOwner::State::Failed)
      fail();
    else if (state == ExFatStageOwner::State::Ready) {
      if (!identityMatches() ||
          !owner_.beginStage(request_.metadata().stageName(), request_.metadata().size(), reserve_))
        fail();
      else {
        state_ = State::Body;
        if (request_.metadata().size() == 0) finish();
      }
    }
    return state_;
  }
  void cancel() {
    if (state_ != State::Complete) fail();
  }
  State state() const { return state_; }

 private:
  bool active() const {
    return callbacks_.active && callbacks_.active(callbacks_.context) && !cancelled_.test(std::memory_order_relaxed);
  }
  static bool activeCallback(void* context) { return static_cast<GuardedRawStageSession*>(context)->active(); }
  bool identityMatches() const {
    return callbacks_.verifyIdentity && callbacks_.verifyIdentity(callbacks_.context, request_.metadata().deviceId());
  }
  bool flush() {
    if (!active() || !owner_.append(buffer_, buffered_)) return fail();
    buffered_ = 0;
    return true;
  }
  bool finish() {
    if ((buffered_ && !flush()) || !active() || !owner_.finish() || !identityMatches()) return fail();
    state_ = State::Complete;
    return true;
  }
  bool fail() {
    state_ = State::Failed;
    return false;
  }
  ExFatStageOwner owner_;
  RawStageRequest request_;
  uint8_t* buffer_;
  size_t capacity_;
  uint64_t reserve_;
  Callbacks callbacks_;
  const std::atomic_flag& cancelled_;
  uint64_t received_ = 0;
  size_t buffered_ = 0;
  State state_ = State::Headers;
};
}  // namespace wirecheck
