#pragma once

#include "DailyReadingStatsCodec.h"
#include "ReadingStatsUtils.h"

namespace dailyreading {
// Reader-owned, fixed memory. Timer delivery never performs storage I/O.
class Session {
 public:
  using Load = bool (*)(void*, uint32_t, Counter&);
  using Save = bool (*)(void*, uint32_t, const Counter&);
  void add(const ReadingStatsDateTime* start, uint32_t seconds, uint32_t sessionSeconds);
  bool flush(void* context, Load load, Save save);
  // Drop only sub-floor time; accepted counters survive a failed save and retry.
  void endSession();

 private:
  struct Day {
    uint32_t day = 0;
    uint32_t pending = 0;
    uint32_t unwritten = 0;
    Counter target;
    bool based = false;
    bool used = false;
  };
  std::array<Day, 10> days{};
  bool qualified = false;
  bool dropped = false;
};
static_assert(sizeof(Session) <= 256);
}  // namespace dailyreading
