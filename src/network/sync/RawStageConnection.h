#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "RawStageSocket.h"

namespace wirecheck {
// The owner must close the borrowed descriptor after a terminal result.
class RawStageConnection {
 public:
  enum class Result { Pending, Staged, Rejected, ResponseLost };

  RawStageConnection(HalStorage& storage, int descriptor, uint8_t* buffer, size_t capacity,
                     RawStageSocket::Callbacks callbacks, uint32_t start)
      : descriptor(descriptor), callbacks(callbacks), socket(storage, descriptor, buffer, capacity, callbacks, start) {}
  RawStageConnection(const RawStageConnection&) = delete;
  RawStageConnection& operator=(const RawStageConnection&) = delete;

  Result poll() {
    if (result != Result::Pending) return result;
    if (!callbacks.now || !callbacks.active || !callbacks.active(callbacks.context) || descriptor < 0) return lost();
    if (!response) {
      const auto state = socket.poll();
      if (state == RawStagePump::Result::Pending) return result;
      staged = state == RawStagePump::Result::Complete;
      response = staged ? STAGED_RESPONSE : REJECTED_RESPONSE;
      responseLength = std::strlen(response);
      responseStarted = callbacks.now(callbacks.context);
    }
    if (callbacks.now(callbacks.context) - responseStarted >= RESPONSE_TIMEOUT_MS) return lost();
    const int count = send(descriptor, response + sent, responseLength - sent, MSG_DONTWAIT);
    if (count > 0) {
      if (static_cast<size_t>(count) > responseLength - sent) return lost();
      sent += static_cast<size_t>(count);
      if (sent == responseLength) result = staged ? Result::Staged : Result::Rejected;
    } else if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
      return lost();
    }
    return result;
  }

  void cancel() {
    if (result == Result::Pending) lost();
  }
  Result state() const { return result; }

 private:
  static constexpr uint32_t RESPONSE_TIMEOUT_MS = 2000;
  static constexpr const char* STAGED_RESPONSE =
      "HTTP/1.1 201 Created\r\nContent-Type: application/json\r\nContent-Length: 15\r\n"
      "Cache-Control: no-store\r\nConnection: close\r\n\r\n{\"staged\":true}";
  static constexpr const char* REJECTED_RESPONSE =
      "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: 2\r\n"
      "Cache-Control: no-store\r\nConnection: close\r\n\r\n{}";
  Result lost() {
    socket.cancel();
    result = Result::ResponseLost;
    return result;
  }

  int descriptor;
  RawStageSocket::Callbacks callbacks;
  RawStageSocket socket;
  const char* response = nullptr;
  size_t responseLength = 0;
  size_t sent = 0;
  uint32_t responseStarted = 0;
  Result result = Result::Pending;
  bool staged = false;
};
}  // namespace wirecheck
