#pragma once
#include <ctime>
struct HalClock {
  bool localTime(struct tm&) const { return false; }
};
inline HalClock halClock;
