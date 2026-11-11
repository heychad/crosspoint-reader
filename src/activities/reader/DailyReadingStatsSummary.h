#pragma once
#include <cstdint>

namespace dailyreading {
struct Summary {
  uint32_t todaySeconds = 0;
  uint32_t sevenDayAverageSeconds = 0;
  bool hasToday = false;
  bool hasSevenDayAverage = false;
  bool incomplete = false;
};
Summary summarize(uint32_t today);
}  // namespace dailyreading
