#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <sys/socket.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>

#include "GuardedRawStagePump.h"

namespace wirecheck {
// Borrows an exclusively owned nonblocking socket. The caller closes it after
// completion or failure. The callback context, buffer, and cancellation flag
// outlive this object. now and start use the same monotonic, wrapping millisecond clock.
class GuardedRawStageSocket {
 public:
  struct Callbacks {
    void* context;
    uint32_t (*now)(void*);
    bool (*active)(void*);
    bool (*verifyIdentity)(void*, const char*);
  };

  GuardedRawStageSocket(HalStorage& storage, int descriptor, uint8_t* buffer, size_t capacity, uint64_t reserve,
                        Callbacks callbacks, const std::atomic_flag& cancelled, uint32_t start)
      : descriptor_(descriptor),
        callbacks_(callbacks),
        pump_(storage, buffer, capacity, reserve, {this, now, read, active, verifyIdentity}, cancelled, start) {}
  GuardedRawStageSocket(const GuardedRawStageSocket&) = delete;
  GuardedRawStageSocket& operator=(const GuardedRawStageSocket&) = delete;

  GuardedRawStagePump::Result poll() { return pump_.poll(); }
  void cancel() { pump_.cancel(); }

 private:
  static uint32_t now(void* context) {
    auto& self = *static_cast<GuardedRawStageSocket*>(context);
    return self.callbacks_.now ? self.callbacks_.now(self.callbacks_.context) : 0;
  }
  static bool active(void* context) {
    auto& self = *static_cast<GuardedRawStageSocket*>(context);
    return self.descriptor_ >= 0 && self.callbacks_.now && self.callbacks_.active && self.callbacks_.verifyIdentity &&
           self.callbacks_.active(self.callbacks_.context);
  }
  static bool verifyIdentity(void* context, const char* id) {
    auto& self = *static_cast<GuardedRawStageSocket*>(context);
    return self.callbacks_.verifyIdentity && self.callbacks_.verifyIdentity(self.callbacks_.context, id);
  }
  static int read(void* context, uint8_t* out, size_t capacity) {
    auto& self = *static_cast<GuardedRawStageSocket*>(context);
    const int count = recv(self.descriptor_, out, capacity, MSG_DONTWAIT);
    if (count > 0) return count;
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
    return -1;
  }
  int descriptor_;
  Callbacks callbacks_;
  GuardedRawStagePump pump_;
};
}  // namespace wirecheck
