#pragma once

#include <cstdint>

struct HomeDashboardData {
  int progress = -1;
  uint32_t readingSeconds = 0;
  uint32_t remainingSeconds = 0;
  bool hasStats = false;
};
