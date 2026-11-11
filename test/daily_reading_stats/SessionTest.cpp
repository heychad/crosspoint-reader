#include <gtest/gtest.h>

#include <map>
#include <set>

#include "activities/reader/DailyReadingStatsSession.h"

namespace {
using namespace dailyreading;
struct IO {
  std::map<uint32_t, Counter> counters;
  std::set<uint32_t> failLoad, failSave;
  bool applyFailedSave = false;
  unsigned loads = 0, saves = 0;
  static bool load(void* raw, uint32_t day, Counter& value) {
    auto& io = *static_cast<IO*>(raw);
    ++io.loads;
    if (io.failLoad.contains(day)) return false;
    value = io.counters[day];
    return true;
  }
  static bool save(void* raw, uint32_t day, const Counter& value) {
    auto& io = *static_cast<IO*>(raw);
    ++io.saves;
    const bool fail = io.failSave.contains(day);
    if (!fail || io.applyFailedSave) io.counters[day] = value;
    return !fail;
  }
};
const ReadingStatsDateTime noon{{2026, 11, 15}, 12, 0, 0};
const uint32_t day = readingStatsDayIndex(noon.date);
TEST(DailySession, AddNeverCallsStorageAndFloorPromotesEarlierIntervals) {
  Session session;
  IO io;
  io.counters[day] = {100, 20};
  session.add(&noon, 4, 4);
  EXPECT_TRUE(session.flush(&io, IO::load, IO::save));
  EXPECT_EQ(io.loads, 0U);
  session.add(&noon, 6, 10);
  EXPECT_EQ(io.saves, 0U);
  EXPECT_TRUE(session.flush(&io, IO::load, IO::save));
  EXPECT_EQ(io.counters[day], (Counter{110, 20}));
  EXPECT_TRUE(session.flush(&io, IO::load, IO::save));
  EXPECT_EQ(io.saves, 1U);
}
TEST(DailySession, SubFloorSessionsDoNotCombineAcrossFinish) {
  Session session;
  IO io;
  session.add(&noon, 4, 4);
  session.endSession();
  session.add(&noon, 6, 6);
  session.endSession();
  EXPECT_TRUE(session.flush(&io, IO::load, IO::save));
  EXPECT_EQ(io.saves, 0U);
}
TEST(DailySession, MidnightAndLeapDaySplitDatedSeconds) {
  const ReadingStatsDateTime start{{2028, 2, 29}, 23, 59, 55};
  const uint32_t leap = readingStatsDayIndex(start.date);
  Session session;
  IO io;
  session.add(&start, 12, 12);
  EXPECT_TRUE(session.flush(&io, IO::load, IO::save));
  EXPECT_EQ(io.counters[leap].seconds, 5U);
  EXPECT_EQ(io.counters[leap + 1].seconds, 7U);
}
TEST(DailySession, UncertainSaveRetryDoesNotReadBackAndDoubleTheDelta) {
  Session session;
  IO io;
  io.counters[day] = {100, 20};
  io.failSave.insert(day);
  io.applyFailedSave = true;
  session.add(&noon, 20, 20);
  EXPECT_FALSE(session.flush(&io, IO::load, IO::save));
  EXPECT_EQ(io.counters[day].seconds, 120U);
  session.add(&noon, 10, 30);
  io.failSave.clear();
  EXPECT_TRUE(session.flush(&io, IO::load, IO::save));
  EXPECT_EQ(io.counters[day], (Counter{130, 20}));
  EXPECT_EQ(io.loads, 1U);
}
TEST(DailySession, LoadFailureAndFinishRetainAcceptedButNotSubFloorTime) {
  Session session;
  IO io;
  io.failLoad.insert(day);
  session.add(&noon, 20, 20);
  EXPECT_FALSE(session.flush(&io, IO::load, IO::save));
  session.endSession();
  session.add(&noon, 5, 5);
  io.failLoad.clear();
  EXPECT_TRUE(session.flush(&io, IO::load, IO::save));
  EXPECT_EQ(io.counters[day].seconds, 20U);
  session.endSession();
  EXPECT_TRUE(session.flush(&io, IO::load, IO::save));
  EXPECT_EQ(io.counters[day].seconds, 20U);
}
TEST(DailySession, UnknownClockDoesNotInventDatesButStillQualifiesSession) {
  Session session;
  IO io;
  session.add(nullptr, 10, 10);
  EXPECT_TRUE(session.flush(&io, IO::load, IO::save));
  EXPECT_EQ(io.loads, 0U);
  session.add(&noon, 2, 12);
  EXPECT_TRUE(session.flush(&io, IO::load, IO::save));
  EXPECT_EQ(io.counters[day].seconds, 2U);
}
TEST(DailySession, InvalidCounterAndLongIntervalReportFailure) {
  Session session;
  IO io;
  io.counters[day] = {1, 2};
  session.add(&noon, 10, 10);
  EXPECT_FALSE(session.flush(&io, IO::load, IO::save));
  EXPECT_EQ(io.saves, 0U);
  io.counters[day] = {};
  EXPECT_TRUE(session.flush(&io, IO::load, IO::save));
  session.add(&noon, 601, 611);
  EXPECT_FALSE(session.flush(&io, IO::load, IO::save));
}
TEST(DailySession, CapacityFailureRemainsVisibleWhileOtherDaysSave) {
  Session session;
  IO io;
  auto date = noon;
  for (unsigned i = 0; i < 11; ++i) {
    session.add(&date, 10, 10);
    addDaysToReadingStatsDate(date.date, 1);
  }
  EXPECT_FALSE(session.flush(&io, IO::load, IO::save));
  EXPECT_EQ(io.saves, 10U);
}
TEST(DailySession, CounterSaturatesInsteadOfWrapping) {
  Session session;
  IO io;
  io.counters[day] = {UINT32_MAX - 1, 20};
  session.add(&noon, 10, 10);
  EXPECT_TRUE(session.flush(&io, IO::load, IO::save));
  EXPECT_EQ(io.counters[day], (Counter{UINT32_MAX, 20}));
}
}  // namespace
