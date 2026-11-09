#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

#include "RawStageConnection.h"

namespace wirecheck {
// Owns one listener and at most one connection. Its owner must poll, stop it
// when network ownership ends, and avoid blocking other network services.
class RawStageListener {
 public:
  RawStageListener(HalStorage& storage, RawStageSocket::Callbacks callbacks) : storage(storage), callbacks(callbacks) {}
  ~RawStageListener() { stop(); }
  RawStageListener(const RawStageListener&) = delete;
  RawStageListener& operator=(const RawStageListener&) = delete;

  bool begin(uint16_t port) {
    if (listener >= 0 || !callbacks.now || !callbacks.active || !callbacks.verifyIdentity) return false;
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (!nonblocking(fd) || ::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(fd, 1) != 0) {
      ::close(fd);
      return false;
    }
    listener = fd;
    hasResult = false;
    return true;
  }

  void poll() {
    if (listener < 0) return;
    if (!callbacks.active(callbacks.context)) {
      stop();
      return;
    }
    if (!connection) {
      const int accepted = ::accept(listener, nullptr, nullptr);
      if (accepted < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) stop();
        return;
      }
      if (!nonblocking(accepted)) {
        ::close(accepted);
        return;
      }
      hasResult = false;
      buffer.reset(new (std::nothrow) uint8_t[BUFFER_SIZE]);
      if (buffer)
        connection.reset(new (std::nothrow) RawStageConnection(storage, accepted, buffer.get(), BUFFER_SIZE, callbacks,
                                                               callbacks.now(callbacks.context)));
      if (!connection) {
        buffer.reset();
        ::close(accepted);
        return;
      }
      client = accepted;
    }
    const auto result = connection->poll();
    if (result != RawStageConnection::Result::Pending) {
      lastResult = result;
      hasResult = true;
      closeClient();
    }
  }

  void stop() {
    closeClient();
    if (listener >= 0) ::close(listener);
    listener = -1;
  }
  bool running() const { return listener >= 0; }
  bool busy() const { return connection != nullptr; }
  bool completed() const { return hasResult; }
  RawStageConnection::Result result() const { return lastResult; }
  uint16_t localPort() const {
    if (listener < 0) return 0;
    sockaddr_in address{};
    socklen_t size = sizeof(address);
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) != 0 || size != sizeof(address)) return 0;
    return ntohs(address.sin_port);
  }

 private:
  static constexpr size_t BUFFER_SIZE = 16384;
  static bool nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
  }
  void closeClient() {
    if (connection) connection->cancel();
    connection.reset();
    buffer.reset();
    if (client >= 0) ::close(client);
    client = -1;
  }

  HalStorage& storage;
  RawStageSocket::Callbacks callbacks;
  int listener = -1;
  int client = -1;
  std::unique_ptr<uint8_t[]> buffer;
  std::unique_ptr<RawStageConnection> connection;
  RawStageConnection::Result lastResult = RawStageConnection::Result::Pending;
  bool hasResult = false;
};
}  // namespace wirecheck
