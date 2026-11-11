#pragma once
#include <ctime>
inline bool dailyClockEnabled = false;
inline struct tm dailyClock{};
struct HalClock {
  bool localTime(struct tm& out) const {
    out = dailyClock;
    return dailyClockEnabled;
  }
};
inline HalClock halClock;
