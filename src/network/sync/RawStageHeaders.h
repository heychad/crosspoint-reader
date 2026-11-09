#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "ExclusiveStage.h"

namespace wirecheck {
// Request metadata for the experimental raw staging transport. The caller
// must bound each line before passing it here and must stop on rejection.
class RawStageHeaders {
 public:
  bool add(std::string_view line) {
    if (failed || ++lines > 32 || line.size() > 512 || totalBytes + line.size() + 2 > 4096) return reject();
    totalBytes += line.size() + 2;
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos || colon == 0) return reject();
    const auto name = line.substr(0, colon);
    for (const unsigned char c : name) {
      if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return reject();
    }
    auto value = line.substr(colon + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
    for (const unsigned char c : value)
      if (c < 32 || c == 127) return reject();
    if (equalsName(name, "transfer-encoding")) return reject();
    if (equalsName(name, "content-length")) {
      if (seenLength || value.empty() || (value.size() > 1 && value.front() == '0')) return reject();
      uint64_t size = 0;
      for (const char c : value) {
        if (c < '0' || c > '9' || size > (MAX_SIZE - static_cast<unsigned>(c - '0')) / 10) return reject();
        size = size * 10 + static_cast<unsigned>(c - '0');
      }
      expectedSize = size;
      seenLength = true;
    } else if (equalsName(name, "content-type")) {
      if (seenType || value != "application/octet-stream") return reject();
      seenType = true;
    } else if (equalsName(name, "x-xsync-device")) {
      if (seenDevice || value.size() != 64) return reject();
      for (const char c : value)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return reject();
      std::memcpy(device, value.data(), value.size());
      seenDevice = true;
    } else if (equalsName(name, "x-xsync-stage")) {
      if (seenStage || value.size() != 45) return reject();
      std::memcpy(stage, value.data(), value.size());
      if (!ExclusiveStage::validName(stage)) return reject();
      seenStage = true;
    }
    return true;
  }

  bool complete() const { return !failed && seenLength && seenType && seenDevice && seenStage; }
  uint64_t size() const { return expectedSize; }
  const char* deviceId() const { return device; }
  const char* stageName() const { return stage; }

 private:
  static constexpr uint64_t MAX_SIZE = 0x7fffffffULL - 1024;
  static bool equalsName(std::string_view actual, std::string_view expected) {
    if (actual.size() != expected.size()) return false;
    for (size_t i = 0; i < actual.size(); ++i) {
      char c = actual[i];
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
      if (c != expected[i]) return false;
    }
    return true;
  }
  bool reject() {
    failed = true;
    return false;
  }

  char device[65]{};
  char stage[46]{};
  uint64_t expectedSize = 0;
  size_t totalBytes = 0;
  unsigned lines = 0;
  bool failed = false;
  bool seenLength = false;
  bool seenType = false;
  bool seenDevice = false;
  bool seenStage = false;
};
}  // namespace wirecheck
