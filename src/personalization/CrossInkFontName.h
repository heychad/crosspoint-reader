#pragma once

#include <cstddef>
#include <cstdint>

namespace crossink {
inline bool validUtf8(const uint8_t* bytes, size_t length) {
  for (size_t i = 0; i < length;) {
    const uint8_t lead = bytes[i++];
    if (lead < 0x80) continue;
    unsigned remaining;
    uint32_t codepoint;
    if (lead >= 0xc2 && lead <= 0xdf) {
      remaining = 1;
      codepoint = lead & 0x1f;
    } else if (lead >= 0xe0 && lead <= 0xef) {
      remaining = 2;
      codepoint = lead & 0x0f;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
      remaining = 3;
      codepoint = lead & 0x07;
    } else {
      return false;
    }
    if (remaining > length - i) return false;
    const unsigned count = remaining;
    while (remaining--) {
      const uint8_t next = bytes[i++];
      if ((next & 0xc0) != 0x80) return false;
      codepoint = (codepoint << 6) | (next & 0x3f);
    }
    if ((count == 2 && codepoint < 0x800) || (count == 3 && codepoint < 0x10000) ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff)
      return false;
  }
  return true;
}

inline bool compatibleFontName(const char* name, size_t length) {
  // Native settings reserve 32 bytes, including the terminator.
  if (!name || length == 0 || length >= 32 || name[0] == '.') return false;
  for (size_t i = 0; i < length; ++i) {
    const auto byte = static_cast<uint8_t>(name[i]);
    if (byte < 0x20 || byte == 0x7f || name[i] == '/' || name[i] == '\\') return false;
  }
  return validUtf8(reinterpret_cast<const uint8_t*>(name), length);
}
}  // namespace crossink
