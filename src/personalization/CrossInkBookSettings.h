#pragma once

#include <cstddef>
#include <cstdint>

namespace crossink {
// Only the subset with an exact native value is proposed. Unmapped bits and
// flags remain visible to the caller; decoding never modifies the source file.
struct BookSettingsPreview {
  uint32_t nativeMask = 0;
  uint32_t unmappedMask = 0;
  uint8_t values[15]{};
  char font[32]{};
  uint8_t indexingMethod = 0;
  uint8_t verticalMargin = 5;
  uint8_t version = 0;
  uint8_t extraFlags = 0;
};
bool decodeBookSettings(const uint8_t* bytes, size_t length, BookSettingsPreview& output);
}  // namespace crossink
