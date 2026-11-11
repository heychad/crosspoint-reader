#include <Arduino.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <ReaderStats.h>
#include <ReadingStatsStore.h>
#include <gtest/gtest.h>

#include "activities/reader/DailyReadingStatsStore.h"

namespace {
class DailyRuntime : public testing::Test {
 protected:
  const std::string book = "/Books/daily.xtc";
  const std::string cache = "/.crosspoint/xtc_" + std::to_string(std::hash<std::string>{}(book));
  const uint32_t day = readingStatsDayIndex({2026, 11, 15});
  void SetUp() override {
    Storage.reset();
    testMillis = 0;
    dailyClockEnabled = true;
    dailyClock = {};
    dailyClock.tm_year = 126;
    dailyClock.tm_mon = 10;
    dailyClock.tm_mday = 15;
    dailyClock.tm_hour = 12;
    HalFile file;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", book, file));
    ASSERT_TRUE(file.close());
  }
  void TearDown() override { dailyClockEnabled = false; }
  uint32_t daily(uint32_t date) {
    dailyreading::Counter value;
    EXPECT_EQ(dailyreading::read(date, value), dailyreading::ReadResult::Available);
    return value.seconds;
  }
};
TEST_F(DailyRuntime, PauseAndResumeExcludeMenuTimeAndKeepBookTotals) {
  ReaderStats stats;
  stats.begin(cache, book, true, 300);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 20000;
  stats.pause();
  EXPECT_EQ(daily(day), 20U);
  testMillis = 500000;
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 510000;
  ASSERT_TRUE(stats.finish());
  EXPECT_EQ(daily(day), 30U);
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(cache)).totalReadingSeconds, 30U);
  EXPECT_EQ(GlobalReadingStats::load().totalReadingSeconds, 30U);
}
TEST_F(DailyRuntime, MidnightSplitUsesSameEligibleTimer) {
  dailyClock.tm_hour = 23;
  dailyClock.tm_min = 59;
  dailyClock.tm_sec = 55;
  ReaderStats stats;
  stats.begin(cache, book, true, 300);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 12000;
  ASSERT_TRUE(stats.finish());
  EXPECT_EQ(daily(day), 5U);
  EXPECT_EQ(daily(day + 1), 7U);
}
TEST_F(DailyRuntime, IdleAndShortSessionsDoNotCreateDailyFiles) {
  ReaderStats stats;
  stats.begin(cache, book, true, 300);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 301000;
  ASSERT_TRUE(stats.finish());
  EXPECT_FALSE(Storage.exists(dailyreading::DIRECTORY));
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 306000;
  ASSERT_TRUE(stats.finish());
  EXPECT_FALSE(Storage.exists(dailyreading::DIRECTORY));
}
TEST_F(DailyRuntime, FailedDailySaveKeepsOtherStatisticsAndCanRetry) {
  ReaderStats stats;
  stats.begin(cache, book, true, 300);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 20000;
  stats.pause();
  ASSERT_EQ(daily(day), 20U);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 30000;
  Storage.failNextNewSyncFor("/.crosspoint/daily_reading/2026-11.b.bin");
  EXPECT_FALSE(stats.finish());
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(cache)).totalReadingSeconds, 30U);
  EXPECT_TRUE(stats.finish());
  EXPECT_EQ(daily(day), 30U);
}
}  // namespace
