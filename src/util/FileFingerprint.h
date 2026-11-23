#pragma once

#include <cstddef>
#include <cstdint>

namespace filefingerprint {
inline constexpr uint64_t OFFSET = 14695981039346656037ULL;

inline uint64_t append(uint64_t fingerprint, const uint8_t* bytes, size_t size) {
  for (size_t i = 0; i < size; ++i) fingerprint = (fingerprint ^ bytes[i]) * 1099511628211ULL;
  return fingerprint;
}
}  // namespace filefingerprint
