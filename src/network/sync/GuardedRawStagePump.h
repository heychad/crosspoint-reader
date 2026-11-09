#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "GuardedRawStageSession.h"

namespace wirecheck {
// One task owns this pump, its body buffer, and the borrowed connection.
// read must not block: positive bytes, zero if idle, or -1 on EOF/error.
// The callback context and cancellation flag outlive this object. now and
// start use the same monotonic, wrapping 32-bit millisecond clock.
class GuardedRawStagePump {
 public:
  enum class Result { Pending, Complete, Failed };
  struct Callbacks {
    void* context;
    uint32_t (*now)(void*);
    int (*read)(void*, uint8_t*, size_t);
    bool (*active)(void*);
    bool (*verifyIdentity)(void*, const char*);
  };

  GuardedRawStagePump(HalStorage& storage, uint8_t* buffer, size_t capacity, uint64_t reserve, Callbacks callbacks,
                      const std::atomic_flag& cancelled, uint32_t start)
      : session_(storage, buffer, capacity, reserve, {callbacks.context, callbacks.active, callbacks.verifyIdentity},
                 cancelled),
        callbacks_(callbacks),
        cancelled_(cancelled),
        started_(start),
        lastData_(start) {
    if (!callbacks_.now || !callbacks_.read || session_.state() == GuardedRawStageSession::State::Failed)
      result_ = Result::Failed;
  }

  Result poll() {
    using State = GuardedRawStageSession::State;
    if (result_ != Result::Pending) return result_;
    const uint32_t now = callbacks_.now(callbacks_.context);
    const auto initial = session_.state();
    if (cancelled_.test(std::memory_order_relaxed) || !callbacks_.active(callbacks_.context) ||
        now - started_ >= TOTAL_TIMEOUT_MS || (initial != State::Preparing && now - lastData_ >= IDLE_TIMEOUT_MS) ||
        (initial == State::Headers && now - started_ >= HEADER_TIMEOUT_MS))
      return fail();
    if (initial == State::Preparing) {
      const auto state = session_.step();
      if (state == State::Failed) return fail();
      if (state == State::Complete) {
        result_ = Result::Complete;
        return result_;
      }
      if (state == State::Preparing) return result_;
      lastData_ = now;
    }
    if (used_ == available_) {
      const int count = callbacks_.read(callbacks_.context, input_, sizeof(input_));
      if (count < 0 || static_cast<size_t>(count) > sizeof(input_)) return fail();
      if (!count) return result_;
      used_ = 0;
      available_ = static_cast<size_t>(count);
      lastData_ = now;
    }
    const size_t count = session_.consume(input_ + used_, available_ - used_);
    used_ += count;
    switch (session_.state()) {
      case State::Preparing:
        return result_;
      case State::Complete:
        result_ = Result::Complete;
        return result_;
      case State::Failed:
        return fail();
      case State::Headers:
      case State::Body:
        return count ? result_ : fail();
    }
    return fail();
  }
  void cancel() {
    if (result_ == Result::Pending) fail();
  }
  Result state() const { return result_; }

 private:
  static constexpr uint32_t HEADER_TIMEOUT_MS = 15000;
  static constexpr uint32_t IDLE_TIMEOUT_MS = 30000;
  static constexpr uint32_t TOTAL_TIMEOUT_MS = 3600000;
  Result fail() {
    session_.cancel();
    result_ = Result::Failed;
    return result_;
  }
  GuardedRawStageSession session_;
  Callbacks callbacks_;
  const std::atomic_flag& cancelled_;
  uint8_t input_[128]{};
  size_t used_ = 0, available_ = 0;
  uint32_t started_, lastData_;
  Result result_ = Result::Pending;
};
}  // namespace wirecheck
