#pragma once

#include <cstddef>
#include <cstdint>

namespace readerlinespacing {
inline constexpr uint8_t MIN_PERCENT = 70;
inline constexpr uint8_t MAX_PERCENT = 200;
inline constexpr uint8_t DEFAULT_PERCENT = 120;
inline constexpr uint8_t PRESET_PERCENTAGES[] = {100, 110, 120, 130, 145, 160, 175};

constexpr bool valid(int percent) { return percent >= MIN_PERCENT && percent <= MAX_PERCENT; }

constexpr uint8_t fromPreset(int index) {
  return index >= 0 && index < static_cast<int>(sizeof(PRESET_PERCENTAGES)) ? PRESET_PERCENTAGES[index]
                                                                            : DEFAULT_PERCENT;
}

// Older builds can read the closest preset from a percentage-based save.
constexpr uint8_t nearestPreset(int percent) {
  uint8_t index = 0;
  while (static_cast<size_t>(index) + 1 < sizeof(PRESET_PERCENTAGES) &&
         2 * percent > PRESET_PERCENTAGES[index] + PRESET_PERCENTAGES[index + 1]) {
    ++index;
  }
  return index;
}
}  // namespace readerlinespacing
