#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "RawStageHeaders.h"

namespace wirecheck {
// The receiver owns the deadline and allocates this object off a small task stack.
class RawStageRequest {
 public:
  enum class Result { Pending, Complete, Rejected };

  Result consume(uint8_t byte) {
    if (result != Result::Pending) return result;
    if (awaitingLf) {
      if (byte != '\n') return reject();
      awaitingLf = false;
      const std::string_view line(buffer, used);
      if (firstLine) {
        if (line != "PUT /api/sync/stage-raw HTTP/1.1") return reject();
        firstLine = false;
      } else if (line.empty()) {
        if (!headers.complete()) return reject();
        result = Result::Complete;
      } else if (!headers.add(line)) {
        return reject();
      }
      used = 0;
      return result;
    }
    if (byte == '\r') {
      awaitingLf = true;
      return result;
    }
    if (byte == '\n' || byte == 0 || used == sizeof(buffer)) return reject();
    buffer[used++] = static_cast<char>(byte);
    return result;
  }

  Result state() const { return result; }
  const RawStageHeaders& metadata() const { return headers; }

 private:
  Result reject() {
    result = Result::Rejected;
    return result;
  }

  RawStageHeaders headers;
  char buffer[512]{};
  size_t used = 0;
  Result result = Result::Pending;
  bool firstLine = true;
  bool awaitingLf = false;
};
}  // namespace wirecheck
