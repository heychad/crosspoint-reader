#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace personalupdate {
inline constexpr size_t MAX_RELEASE_BYTES = 64 * 1024;

struct Release {
  char version[27]{};
  char url[160]{};
  uint32_t size = 0;
  std::array<uint8_t, 32> sha256{};
};

enum class ReleaseResult { Valid, MissingAsset, Invalid, NoMemory };

// Consumes a mutable JSON buffer. Output owns its values and is cleared on failure.
ReleaseResult parseRelease(char* json, size_t length, size_t maxImageBytes, Release& output);
}  // namespace personalupdate
