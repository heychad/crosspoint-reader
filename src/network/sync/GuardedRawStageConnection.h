#pragma once

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "GuardedRawStageSocket.h"

namespace wirecheck {
// Borrows a nonblocking descriptor; the owner closes it after a terminal
// result. Staged means the local socket accepted the response, not that the
// client received it. Rejected may leave a partial stage. Reconcile ambiguous
// outcomes before retrying.
class GuardedRawStageConnection {
 public:
  enum class Result { Pending, Staged, Rejected, ResponseLost };

  GuardedRawStageConnection(HalStorage& storage, int descriptor, uint8_t* buffer, size_t capacity, uint64_t reserve,
                            GuardedRawStageSocket::Callbacks callbacks, const std::atomic_flag& cancelled,
                            uint32_t start)
      : descriptor_(descriptor),
        callbacks_(callbacks),
        cancelled_(cancelled),
        socket_(storage, descriptor, buffer, capacity, reserve, callbacks, cancelled, start) {
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    sendSafe_ = descriptor_ >= 0 && setsockopt(descriptor_, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) == 0;
#endif
  }
  GuardedRawStageConnection(const GuardedRawStageConnection&) = delete;
  GuardedRawStageConnection& operator=(const GuardedRawStageConnection&) = delete;

  Result poll() {
    if (result_ != Result::Pending) return result_;
    if (!sendSafe_ || !callbacks_.now || !callbacks_.active || !callbacks_.active(callbacks_.context) ||
        descriptor_ < 0 || cancelled_.test(std::memory_order_relaxed))
      return lost();
    if (!response_) {
      const auto state = socket_.poll();
      if (state == GuardedRawStagePump::Result::Pending) return result_;
      staged_ = state == GuardedRawStagePump::Result::Complete;
      response_ = staged_ ? STAGED_RESPONSE : REJECTED_RESPONSE;
      responseLength_ = std::strlen(response_);
      responseStarted_ = callbacks_.now(callbacks_.context);
    }
    if (callbacks_.now(callbacks_.context) - responseStarted_ >= RESPONSE_TIMEOUT_MS) return lost();
    int flags = MSG_DONTWAIT;
#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif
    const int count = send(descriptor_, response_ + sent_, responseLength_ - sent_, flags);
    if (count > 0) {
      if (static_cast<size_t>(count) > responseLength_ - sent_) return lost();
      sent_ += static_cast<size_t>(count);
      if (sent_ == responseLength_) result_ = staged_ ? Result::Staged : Result::Rejected;
    } else if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
      return lost();
    }
    return result_;
  }
  void cancel() {
    if (result_ == Result::Pending) lost();
  }
  Result state() const { return result_; }

 private:
  static constexpr uint32_t RESPONSE_TIMEOUT_MS = 2000;
  static constexpr const char* STAGED_RESPONSE =
      "HTTP/1.1 201 Created\r\nContent-Type: application/json\r\nContent-Length: 15\r\n"
      "Cache-Control: no-store\r\nConnection: close\r\n\r\n{\"staged\":true}";
  static constexpr const char* REJECTED_RESPONSE =
      "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: 2\r\n"
      "Cache-Control: no-store\r\nConnection: close\r\n\r\n{}";
  Result lost() {
    socket_.cancel();
    result_ = Result::ResponseLost;
    return result_;
  }
  int descriptor_;
  GuardedRawStageSocket::Callbacks callbacks_;
  const std::atomic_flag& cancelled_;
  GuardedRawStageSocket socket_;
  const char* response_ = nullptr;
  size_t responseLength_ = 0, sent_ = 0;
  uint32_t responseStarted_ = 0;
  Result result_ = Result::Pending;
  bool staged_ = false;
  bool sendSafe_ = true;
};
}  // namespace wirecheck
