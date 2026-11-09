#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>

#include "GuardedRawStageConnection.h"

namespace wirecheck {
// Owns one listener and at most one connection. The owner polls and stops it,
// and keeps the callback context and cancellation flag alive until destruction.
// It binds all interfaces; the embedding owner must provide an access and
// authentication policy before enablement. Allocate it off a small task stack.
class GuardedRawStageListener {
 public:
  GuardedRawStageListener(HalStorage& storage, uint64_t reserve, GuardedRawStageSocket::Callbacks callbacks,
                          const std::atomic_flag& cancelled)
      : storage_(storage), reserve_(reserve), callbacks_(callbacks), cancelled_(cancelled) {}
  ~GuardedRawStageListener() { stop(); }
  GuardedRawStageListener(const GuardedRawStageListener&) = delete;
  GuardedRawStageListener& operator=(const GuardedRawStageListener&) = delete;

  bool begin(uint16_t port) {
    if (listener_ >= 0 || cancelled_.test(std::memory_order_relaxed) || !callbacks_.now || !callbacks_.active ||
        !callbacks_.verifyIdentity || !callbacks_.active(callbacks_.context))
      return false;
    buffer_.reset(new (std::nothrow) uint8_t[BUFFER_SIZE]);
    if (!buffer_) return false;
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      buffer_.reset();
      return false;
    }
    const int reuse = 1;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0 || !nonblocking(fd) ||
        ::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || ::listen(fd, 1) != 0) {
      ::close(fd);
      buffer_.reset();
      return false;
    }
    listener_ = fd;
    hasResult_ = false;
    return true;
  }
  void poll() {
    if (listener_ < 0) return;
    if (cancelled_.test(std::memory_order_relaxed) || !callbacks_.active(callbacks_.context)) {
      stop();
      return;
    }
    if (!connection_) {
      const int accepted = ::accept(listener_, nullptr, nullptr);
      if (accepted < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) stop();
        return;
      }
      if (!nonblocking(accepted)) {
        ::close(accepted);
        return;
      }
      hasResult_ = false;
      connection_.emplace(storage_, accepted, buffer_.get(), BUFFER_SIZE, reserve_, callbacks_, cancelled_,
                          callbacks_.now(callbacks_.context));
      client_ = accepted;
    }
    const auto result = connection_->poll();
    if (result != GuardedRawStageConnection::Result::Pending) {
      lastResult_ = result;
      hasResult_ = true;
      closeClient();
    }
  }
  void stop() {
    closeClient();
    if (listener_ >= 0) ::close(listener_);
    listener_ = -1;
    buffer_.reset();
  }
  bool running() const { return listener_ >= 0; }
  bool busy() const { return connection_.has_value(); }
  bool completed() const { return hasResult_; }
  GuardedRawStageConnection::Result result() const { return lastResult_; }
  uint16_t localPort() const {
    if (listener_ < 0) return 0;
    sockaddr_in address{};
    socklen_t size = sizeof(address);
    if (::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size) != 0 || size != sizeof(address))
      return 0;
    return ntohs(address.sin_port);
  }

 private:
  static constexpr size_t BUFFER_SIZE = 16384;
  static bool nonblocking(int fd) {
    int enabled = 1;
    return ::ioctl(fd, FIONBIO, &enabled) == 0;
  }
  void closeClient() {
    if (connection_ && connection_->state() == GuardedRawStageConnection::Result::Pending) {
      connection_->cancel();
      lastResult_ = connection_->state();
      hasResult_ = true;
    }
    connection_.reset();
    if (client_ >= 0) ::close(client_);
    client_ = -1;
  }
  HalStorage& storage_;
  uint64_t reserve_;
  GuardedRawStageSocket::Callbacks callbacks_;
  const std::atomic_flag& cancelled_;
  int listener_ = -1, client_ = -1;
  std::unique_ptr<uint8_t[]> buffer_;
  std::optional<GuardedRawStageConnection> connection_;
  GuardedRawStageConnection::Result lastResult_ = GuardedRawStageConnection::Result::Pending;
  bool hasResult_ = false;
};
}  // namespace wirecheck
