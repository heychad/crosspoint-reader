#include "CrossInkBookSettings.h"

#include <cstring>

#include "CrossInkFontName.h"

namespace crossink {
namespace {
constexpr uint32_t ALL_OLD_BITS = (1UL << 19) - 1;
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
  bool name(char* font = nullptr) {
    if (64 > length - pos) return false;
    const auto* end = static_cast<const uint8_t*>(std::memchr(bytes + pos, 0, 64));
    if (!end || !validUtf8(bytes + pos, static_cast<size_t>(end - (bytes + pos)))) return false;
    const size_t size = static_cast<size_t>(end - (bytes + pos));
    if (font && compatibleFontName(reinterpret_cast<const char*>(bytes + pos), size))
      std::memcpy(font, bytes + pos, size + 1);
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
  if (!in.byte(result.version) || result.version < 1 || result.version > 13) return false;
  if (result.version == 1) {
    if (!in.skip(2) || in.pos != length) return false;
    result.extraFlags = bytes[1] || bytes[2] ? 2 : 0;
    output = result;
    return true;
  }
  uint8_t flags, ignored, renderMode;
  if (!in.byte(flags) || (flags & ~0x1f) || !in.skip(2) || !in.byte(renderMode)) return false;
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
  if (!in.name(result.font) || (result.version >= 5 && !in.name()) || (result.version >= 7 && !in.skip(1)))
    return false;
  uint32_t mask = ALL_OLD_BITS;
  if (result.version >= 10) {
    mask = 0;
    for (unsigned i = 0; i < 4; ++i) {
      uint8_t octet;
      if (!in.byte(octet)) return false;
      mask |= static_cast<uint32_t>(octet) << (8 * i);
    }
    const unsigned fieldCount = result.version < 11 ? 19 : result.version + 9;
    if (mask & ~((1UL << fieldCount) - 1)) return false;
  }
  uint8_t imageGrayscale = 1, characterSpacing = 5, publisherFonts = 0;
  if (result.version >= 11 && (!in.byte(imageGrayscale) || imageGrayscale > 1)) return false;
  if (result.version >= 12 && (!in.byte(characterSpacing) || characterSpacing > 10)) return false;
  if (result.version >= 13 && (!in.byte(publisherFonts) || publisherFonts > 2)) return false;
  if (in.pos != length) return false;
  if (!(flags & 1)) mask = 0;
  // Early versions lacked these bytes and inherit their global values.
  if (result.version < 3) mask &= ~(1UL << 3);
  if (result.version < 4) mask &= ~(1UL << 17);
  result.unmappedMask = mask;
  const uint32_t fontBit = 1UL << (result.version < 11 ? 18 : 19);
  if ((mask & fontBit) && result.font[0]) {
    result.nativeMask |= 1UL << 15;
    result.unmappedMask &= ~fontBit;
  } else {
    result.font[0] = '\0';
  }
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
  if ((mask & (1UL << 5)) && old[5] >= 5 && old[5] <= 150) {
    result.verticalMargin = old[5];
    result.nativeMask |= 1U << 18;
    result.unmappedMask &= ~(1UL << 5);
  }
  if ((mask & (1UL << 6)) && old[6] >= 5 && old[6] <= 150) {
    set(result, 5, old[6]);
    result.unmappedMask &= ~(1UL << 6);
  }
  map(8, 6, 4);
  map(9, 10, 1);
  map(10, 9, 1);
  map(11, 11, 1);
  if (result.version < 11 || old[12] != 0) {
    map(12, 12, 2);
  } else if ((mask & ((1UL << 12) | (1UL << 18))) == ((1UL << 12) | (1UL << 18))) {
    // Native image mode combines display and grayscale; require both owners.
    set(result, 12, imageGrayscale ? 0 : 3);
    result.unmappedMask &= ~((1UL << 12) | (1UL << 18));
  }
  if (result.version >= 12 && (mask & (1UL << 20)) && characterSpacing >= 1 && characterSpacing <= 9 &&
      characterSpacing % 2 == 1) {
    // CrossInk stores half-pixel steps around 5; native tracking uses whole pixels.
    set(result, 3, (characterSpacing - 1) / 2);
    result.unmappedMask &= ~(1UL << 20);
  }
  if ((mask & (1UL << 17)) && old[17] <= 1) {
    result.indexingMethod = old[17];
    result.nativeMask |= 1UL << 17;
    result.unmappedMask &= ~(1UL << 17);
  }
  map(13, 7, 1);
  map(15, 13, 1);
  result.extraFlags = flags & ~1;
  if ((flags & 4) && renderMode <= 2) {
    set(result, 14, renderMode);
    result.extraFlags &= ~4;
  }
  output = result;
  return true;
}
}  // namespace crossink
