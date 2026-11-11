#include <HalStorage.h>
#include <gtest/gtest.h>

#include "activities/reader/DailyReadingStatsStore.h"
#include "activities/reader/DailyReadingStatsSummary.h"
#include "activities/reader/ReadingStatsUtils.h"

namespace {
using namespace dailyreading;
class DailySummary : public testing::Test {
  void SetUp() override { Storage.reset(); }
};
TEST_F(DailySummary, MissingHistoryStaysUnknownAndReadsCreateNothing) {
  const auto result = summarize(readingStatsDayIndex({2026, 11, 15}));
  EXPECT_FALSE(result.hasToday);
  EXPECT_FALSE(result.hasSevenDayAverage);
  EXPECT_FALSE(result.incomplete);
  EXPECT_FALSE(Storage.exists(DIRECTORY));
}
TEST_F(DailySummary, StoredZeroDaysContributeToSevenCalendarDayAverage) {
  const uint32_t today = readingStatsDayIndex({2026, 11, 15});
  ASSERT_TRUE(write(today, {140, 0}));
  ASSERT_TRUE(write(today - 2, {70, 0}));
  const auto result = summarize(today);
  EXPECT_TRUE(result.hasToday);
  EXPECT_TRUE(result.hasSevenDayAverage);
  EXPECT_EQ(result.todaySeconds, 140U);
  EXPECT_EQ(result.sevenDayAverageSeconds, 30U);
  EXPECT_FALSE(result.incomplete);
}
TEST_F(DailySummary, MissingPreviousMonthIsNotBackfilledWithZero) {
  const uint32_t today = readingStatsDayIndex({2026, 12, 1});
  ASSERT_TRUE(write(today, {140, 0}));
  auto result = summarize(today);
  EXPECT_TRUE(result.hasToday);
  EXPECT_FALSE(result.hasSevenDayAverage);
  ASSERT_TRUE(write(today - 1, {70, 0}));
  result = summarize(today);
  EXPECT_TRUE(result.hasSevenDayAverage);
  EXPECT_EQ(result.sevenDayAverageSeconds, 30U);
}
TEST_F(DailySummary, ExplicitZeroIsKnownAndUnreadableDaysArePartial) {
  const uint32_t today = readingStatsDayIndex({2026, 11, 15});
  ASSERT_TRUE(write(today, {}));
  auto result = summarize(today);
  EXPECT_TRUE(result.hasToday);
  EXPECT_EQ(result.todaySeconds, 0U);
  char path[64];
  snprintf(path, sizeof(path), "%s/%05lu.bin", DIRECTORY, static_cast<unsigned long>(today - 1));
  Storage.failReadsFor(path);
  result = summarize(today);
  EXPECT_TRUE(result.hasToday);
  EXPECT_FALSE(result.hasSevenDayAverage);
  EXPECT_TRUE(result.incomplete);
}
TEST_F(DailySummary, InvalidDatesAndWideTotalsCannotWrap) {
  auto result = summarize(UINT32_MAX);
  EXPECT_FALSE(result.hasToday);
  EXPECT_FALSE(result.hasSevenDayAverage);
  const uint32_t today = readingStatsDayIndex({2026, 11, 15});
  for (uint32_t offset = 0; offset < 7; ++offset) ASSERT_TRUE(write(today - offset, {UINT32_MAX, 0}));
  result = summarize(today);
  EXPECT_TRUE(result.hasSevenDayAverage);
  EXPECT_EQ(result.sevenDayAverageSeconds, UINT32_MAX);
}
}  // namespace
