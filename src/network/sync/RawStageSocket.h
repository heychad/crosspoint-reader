#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <sys/socket.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>

#include "RawStagePump.h"

namespace wirecheck {
// Borrows an exclusively owned, nonblocking socket descriptor. No other
// reader may buffer its bytes. The owner closes it after any terminal result.
class RawStageSocket {
 public:
  struct Callbacks {
    void* context;
    uint32_t (*now)(void*);
    bool (*active)(void*);
    bool (*verifyIdentity)(void*, const char*);
  };

  RawStageSocket(HalStorage& storage, int descriptor, uint8_t* buffer, size_t capacity, Callbacks callbacks,
                 uint32_t start)
      : descriptor(descriptor),
        callbacks(callbacks),
        pump(storage, buffer, capacity, {this, now, read, active, verifyIdentity}, start) {}
  RawStageSocket(const RawStageSocket&) = delete;
  RawStageSocket& operator=(const RawStageSocket&) = delete;

  RawStagePump::Result poll() { return pump.poll(); }
  void cancel() { pump.cancel(); }

 private:
  static uint32_t now(void* context) {
    auto& self = *static_cast<RawStageSocket*>(context);
    return self.callbacks.now ? self.callbacks.now(self.callbacks.context) : 0;
  }
  static bool active(void* context) {
    auto& self = *static_cast<RawStageSocket*>(context);
    return self.descriptor >= 0 && self.callbacks.now && self.callbacks.active && self.callbacks.verifyIdentity &&
           self.callbacks.active(self.callbacks.context);
  }
  static bool verifyIdentity(void* context, const char* id) {
    auto& self = *static_cast<RawStageSocket*>(context);
    return self.callbacks.verifyIdentity && self.callbacks.verifyIdentity(self.callbacks.context, id);
  }
  static int read(void* context, uint8_t* buffer, size_t capacity) {
    auto& self = *static_cast<RawStageSocket*>(context);
    const int count = recv(self.descriptor, buffer, capacity, MSG_DONTWAIT);
    if (count > 0) return count;
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
    return -1;
  }

  int descriptor;
  Callbacks callbacks;
  RawStagePump pump;
};
}  // namespace wirecheck
