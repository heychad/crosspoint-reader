#include "DailyReadingStatsSession.h"

#include <algorithm>

namespace dailyreading {
namespace {
void addSaturated(uint32_t& total, uint32_t value) { total = UINT32_MAX - total < value ? UINT32_MAX : total + value; }
}  // namespace

void Session::add(const ReadingStatsDateTime* start, uint32_t seconds, uint32_t sessionSeconds) {
  if (!qualified && sessionSeconds >= 10) {
    qualified = true;
    for (auto& day : days) {
      addSaturated(day.unwritten, day.pending);
      day.pending = 0;
    }
  }
  if (!start || !start->isValid() || !seconds) return;
  // The existing page timer caps accepted intervals at its 600-second idle limit.
  if (seconds > 600) {
    dropped = true;
    return;
  }
  uint64_t cursor =
      uint64_t(readingStatsDayIndex(start->date)) * 86400 + start->hour * 3600U + start->minute * 60U + start->second;
  while (seconds) {
    const uint32_t index = cursor / 86400;
    const auto matches = [index](const Day& day) { return day.used && day.day == index; };
    auto slot = std::find_if(days.begin(), days.end(), matches);
    if (slot == days.end()) slot = std::find_if(days.begin(), days.end(), [](const Day& day) { return !day.used; });
    if (slot == days.end()) {
      dropped = true;
      return;
    }
    slot->used = true;
    slot->day = index;
    const uint32_t segment = std::min(seconds, 86400U - static_cast<uint32_t>(cursor % 86400));
    addSaturated(qualified ? slot->unwritten : slot->pending, segment);
    seconds -= segment;
    cursor += segment;
  }
}

bool Session::flush(void* context, Load load, Save save) {
  if (!load || !save) return false;
  bool saved = !dropped;
  for (auto& day : days) {
    if (!day.used || (!day.based && !day.unwritten)) continue;
    if (!day.based) {
      Counter counter;
      if (!load(context, day.day, counter) || !valid(counter)) {
        saved = false;
        continue;
      }
      day.target = counter;
      day.based = true;
    }
    addSaturated(day.target.seconds, day.unwritten);
    day.unwritten = 0;
    if (!save(context, day.day, day.target)) {
      saved = false;
      continue;
    }
    day.based = false;
    day.target = {};
    if (!day.pending) day = {};
  }
  return saved;
}

void Session::endSession() {
  qualified = false;
  for (auto& day : days) {
    day.pending = 0;
    if (!day.based && !day.unwritten) day = {};
  }
}
}  // namespace dailyreading
