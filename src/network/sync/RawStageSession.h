#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ExclusiveStage.h"
#include "RawStageRequest.h"

namespace wirecheck {
// The receiver owns this object and buffer, deadlines, socket closure, and
// connection teardown. A failed transfer leaves its stage for inspection.
class RawStageSession {
 public:
  enum class State { Headers, Body, Complete, Failed };
  struct Callbacks {
    void* context;
    bool (*active)(void*);
    bool (*verifyIdentity)(void*, const char*);
  };

  RawStageSession(HalStorage& storage, uint8_t* buffer, size_t capacity, Callbacks callbacks)
      : stage(storage), buffer(buffer), capacity(capacity), callbacks(callbacks) {
    if (!buffer || !capacity || !callbacks.active || !callbacks.verifyIdentity) state_ = State::Failed;
  }

  // The returned count excludes data after the declared body. Close the socket
  // after every request; this session does not support HTTP keep-alive.
  size_t consume(const uint8_t* data, size_t length) {
    if (state_ == State::Complete || state_ == State::Failed) return 0;
    if ((!data && length) || !callbacks.active(callbacks.context)) {
      fail();
      return 0;
    }
    size_t consumed = 0;
    while (consumed < length && state_ == State::Headers) {
      const auto result = request.consume(data[consumed++]);
      if (result == RawStageRequest::Result::Rejected) {
        fail();
        return consumed;
      }
      if (result == RawStageRequest::Result::Complete) {
        if (!callbacks.active(callbacks.context) ||
            !callbacks.verifyIdentity(callbacks.context, request.metadata().deviceId()) ||
            !stage.begin(request.metadata().stageName(), request.metadata().size())) {
          fail();
          return consumed;
        }
        state_ = State::Body;
        if (request.metadata().size() == 0) finish();
      }
    }
    while (consumed < length && state_ == State::Body) {
      if (!callbacks.active(callbacks.context)) {
        fail();
        break;
      }
      const uint64_t remaining = request.metadata().size() - received;
      const size_t count = std::min({length - consumed, capacity - buffered, static_cast<size_t>(remaining)});
      std::memcpy(buffer + buffered, data + consumed, count);
      consumed += count;
      buffered += count;
      received += count;
      if (buffered == capacity && !flush()) break;
      if (received == request.metadata().size()) finish();
    }
    return consumed;
  }

  void cancel() {
    if (state_ != State::Complete) fail();
  }
  State state() const { return state_; }

 private:
  bool flush() {
    if (!callbacks.active(callbacks.context) || !stage.append(buffer, buffered)) return fail();
    buffered = 0;
    return true;
  }
  bool finish() {
    if ((buffered && !flush()) || !callbacks.active(callbacks.context) || !stage.finish() ||
        !callbacks.verifyIdentity(callbacks.context, request.metadata().deviceId()))
      return fail();
    state_ = State::Complete;
    return true;
  }
  bool fail() {
    state_ = State::Failed;
    return false;
  }

  RawStageRequest request;
  ExclusiveStage stage;
  uint8_t* buffer;
  size_t capacity;
  Callbacks callbacks;
  uint64_t received = 0;
  size_t buffered = 0;
  State state_ = State::Headers;
};
}  // namespace wirecheck
