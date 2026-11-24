#pragma once

#include <cstddef>
#include <cstdint>

#include "../util/Utf8Validation.h"

namespace crossink {
inline bool compatibleFontName(const char* name, size_t length) {
  // Native settings reserve 32 bytes, including the terminator.
  if (!name || length == 0 || length >= 32 || name[0] == '.') return false;
  for (size_t i = 0; i < length; ++i) {
    const auto byte = static_cast<uint8_t>(name[i]);
    if (byte < 0x20 || byte == 0x7f || name[i] == '/' || name[i] == '\\') return false;
  }
  return utf8::isValid(reinterpret_cast<const uint8_t*>(name), length);
}
}  // namespace crossink
