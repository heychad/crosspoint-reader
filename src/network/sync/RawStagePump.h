#pragma once

#include <cstddef>
#include <cstdint>

#include "RawStageSession.h"

namespace wirecheck {
// The receiver owns this object, the supplied staging buffer, and the socket.
// read must return immediately: positive bytes, zero if idle, or -1 on EOF/error.
class RawStagePump {
 public:
  enum class Result { Pending, Complete, Failed };
  struct Callbacks {
    void* context;
    uint32_t (*now)(void*);
    int (*read)(void*, uint8_t*, size_t);
    bool (*active)(void*);
    bool (*verifyIdentity)(void*, const char*);
  };

  RawStagePump(HalStorage& storage, uint8_t* buffer, size_t capacity, Callbacks callbacks, uint32_t start)
      : session(storage, buffer, capacity, {callbacks.context, callbacks.active, callbacks.verifyIdentity}),
        callbacks(callbacks),
        started(start),
        lastData(start) {
    if (!callbacks.now || !callbacks.read || session.state() == RawStageSession::State::Failed) result = Result::Failed;
  }

  Result poll() {
    if (result != Result::Pending) return result;
    const uint32_t now = callbacks.now(callbacks.context);
    if (!callbacks.active(callbacks.context) || now - started >= TOTAL_TIMEOUT_MS ||
        now - lastData >= IDLE_TIMEOUT_MS ||
        (session.state() == RawStageSession::State::Headers && now - started >= HEADER_TIMEOUT_MS))
      return fail();
    uint8_t input[128];
    const int count = callbacks.read(callbacks.context, input, sizeof(input));
    if (count < 0 || static_cast<size_t>(count) > sizeof(input)) return fail();
    if (!count) return result;
    lastData = now;
    session.consume(input, static_cast<size_t>(count));
    switch (session.state()) {
      case RawStageSession::State::Headers:
      case RawStageSession::State::Body:
        return result;
      case RawStageSession::State::Complete:
        result = Result::Complete;
        return result;
      case RawStageSession::State::Failed:
        return fail();
    }
    return fail();
  }

  void cancel() {
    if (result == Result::Pending) fail();
  }
  Result state() const { return result; }

 private:
  static constexpr uint32_t HEADER_TIMEOUT_MS = 15000;
  static constexpr uint32_t IDLE_TIMEOUT_MS = 30000;
  static constexpr uint32_t TOTAL_TIMEOUT_MS = 3600000;
  Result fail() {
    session.cancel();
    result = Result::Failed;
    return result;
  }

  RawStageSession session;
  Callbacks callbacks;
  uint32_t started;
  uint32_t lastData;
  Result result = Result::Pending;
};
}  // namespace wirecheck
