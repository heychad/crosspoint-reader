#include "CrossInkBookSettings.h"

#include <cstring>

namespace crossink {
namespace {
constexpr uint32_t ALL_OLD_BITS = (1UL << 19) - 1;
bool validUtf8(const uint8_t* bytes, size_t length) {
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
struct Cursor {
  const uint8_t* bytes;
  size_t length;
  size_t pos = 0;
  bool byte(uint8_t& value) {
    if (pos >= length) return false;
    value = bytes[pos++];
    return true;
  }
  bool skip(size_t count) {
    if (count > length - pos) return false;
    pos += count;
    return true;
  }
  bool name() {
    if (64 > length - pos) return false;
    const auto* end = static_cast<const uint8_t*>(std::memchr(bytes + pos, 0, 64));
    if (!end || !validUtf8(bytes + pos, static_cast<size_t>(end - (bytes + pos)))) return false;
    pos += 64;
    return true;
  }
};
void set(BookSettingsPreview& result, unsigned index, uint8_t value) {
  result.values[index] = value;
  result.nativeMask |= 1UL << index;
}
}  // namespace

bool decodeBookSettings(const uint8_t* bytes, size_t length, BookSettingsPreview& output) {
  output = {};
  if (!bytes || length == 0 || length > 256) return false;
  Cursor in{bytes, length};
  BookSettingsPreview result;
  if (!in.byte(result.version) || result.version < 1 || result.version > 10) return false;
  if (result.version == 1) {
    if (!in.skip(2) || in.pos != length) return false;
    result.extraFlags = bytes[1] || bytes[2] ? 2 : 0;
    output = result;
    return true;
  }
  uint8_t flags, ignored;
  if (!in.byte(flags) || (flags & ~0x1f) || !in.skip(3)) return false;
  uint8_t old[19]{};
  if (!in.byte(old[0]) || !in.byte(old[1]) || !in.byte(old[2])) return false;
  if (result.version >= 3 && !in.byte(old[3])) return false;
  if (!in.byte(old[4]) || !in.byte(old[5])) return false;
  if (result.version >= 8) {
    if (!in.byte(old[6])) return false;
  } else {
    old[6] = old[5];
  }
  for (unsigned i = 7; i <= 11; ++i)
    if (!in.byte(old[i])) return false;
  if (result.version <= 8 && !in.byte(ignored)) return false;
  for (unsigned i = 12; i <= 16; ++i)
    if (!in.byte(old[i])) return false;
  if (!in.byte(ignored)) return false;  // snapshot render mode
  if (result.version >= 4 && !in.byte(old[17])) return false;
  if (!in.name() || (result.version >= 5 && !in.name()) || (result.version >= 7 && !in.skip(1))) return false;
  uint32_t mask = ALL_OLD_BITS;
  if (result.version >= 10) {
    mask = 0;
    for (unsigned i = 0; i < 4; ++i) {
      uint8_t octet;
      if (!in.byte(octet)) return false;
      mask |= static_cast<uint32_t>(octet) << (8 * i);
    }
    if (mask & ~ALL_OLD_BITS) return false;
  }
  if (in.pos != length) return false;
  if (!(flags & 1)) mask = 0;
  // Early versions lacked these bytes and inherit their global values.
  if (result.version < 3) mask &= ~(1UL << 3);
  if (result.version < 4) mask &= ~(1UL << 17);
  result.unmappedMask = mask;
  const auto map = [&](unsigned source, unsigned target, uint8_t max) {
    if ((mask & (1UL << source)) && old[source] <= max) {
      set(result, target, old[source]);
      result.unmappedMask &= ~(1UL << source);
    }
  };
  if ((mask & (1UL << 1)) && old[1] >= 8) {
    set(result, 0, old[1]);
    result.unmappedMask &= ~(1UL << 1);
  }
  constexpr uint8_t heights[] = {100, 110, 120, 130, 145, 160, 175};
  if (mask & (1UL << 2)) {
    for (uint8_t i = 0; i < sizeof(heights); ++i) {
      if (old[2] == heights[i]) {
        set(result, 1, i);
        result.unmappedMask &= ~(1UL << 2);
        break;
      }
    }
  }
  // CrossInk's zero adds no pixels; the native equivalent is 100 percent.
  if ((mask & (1UL << 3)) && old[3] == 0) {
    set(result, 2, 100);
    result.unmappedMask &= ~(1UL << 3);
  }
  map(4, 4, 3);
  if ((mask & (3UL << 5)) == (3UL << 5) && old[5] == old[6] && old[5] >= 5 && old[5] <= 40) {
    set(result, 5, old[5]);
    result.unmappedMask &= ~(3UL << 5);
  }
  map(8, 6, 4);
  map(9, 10, 1);
  map(10, 9, 1);
  map(11, 11, 1);
  map(12, 12, 2);
  map(13, 7, 1);
  map(15, 13, 1);
  result.extraFlags = flags & ~1;
  output = result;
  return true;
}
}  // namespace crossink
