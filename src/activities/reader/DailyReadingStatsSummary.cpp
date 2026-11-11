#include "DailyReadingStatsSummary.h"

#include "DailyReadingStatsStore.h"
#include "ReadingStatsUtils.h"

namespace dailyreading {
Summary summarize(uint32_t today) {
  Summary result;
  ReadingStatsDate date;
  if (!readingStatsDateFromDayIndex(today, date)) return result;
  result.hasSevenDayAverage = today >= 6;
  uint64_t total = 0;
  for (uint32_t offset = 0; offset < 7 && offset <= today; ++offset) {
    Counter counter;
    const auto status = read(today - offset, counter);
    if (status != ReadResult::Available) result.hasSevenDayAverage = false;
    if (status == ReadResult::Unavailable) result.incomplete = true;
    if (status != ReadResult::Available) continue;
    if (!offset) {
      result.hasToday = true;
      result.todaySeconds = counter.seconds;
    }
    total += counter.seconds;
  }
  if (result.hasSevenDayAverage) result.sevenDayAverageSeconds = total / 7;
  return result;
}
}  // namespace dailyreading
