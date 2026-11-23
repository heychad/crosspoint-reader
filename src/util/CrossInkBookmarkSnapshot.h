#pragma once

#include <HalStorage.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "FileFingerprint.h"

namespace crossinkbookmarks {
inline constexpr size_t MAX_FILE_BYTES = 256 * 1024;

struct Fingerprint {
  size_t size = 0;
  uint64_t checksum = 0;
  bool operator==(const Fingerprint&) const = default;
};

struct RemovalTarget {
  std::string path;
  Fingerprint original;
  uint16_t index = 0;
};

// Caller holds the storage lock and owns the handle.
inline bool captureFingerprint(HalFile& file, Fingerprint& output) {
  if (!file || file.isDirectory()) return false;
  const uint64_t length = file.fileSize64();
  if (length < 2 || length > MAX_FILE_BYTES || !file.seekSet(0)) return false;
  const auto size = static_cast<size_t>(length);
  uint8_t buffer[256];
  uint64_t checksum = filefingerprint::OFFSET;
  for (size_t offset = 0; offset < size;) {
    const size_t count = std::min(sizeof(buffer), size - offset);
    if (file.read(buffer, count) != static_cast<int>(count)) return false;
    checksum = filefingerprint::append(checksum, buffer, count);
    offset += count;
  }
  output = {size, checksum};
  return true;
}
}  // namespace crossinkbookmarks
