#include <I18n.h>
#include <gtest/gtest.h>

#include "activities/reader/ReadingChartModel.h"

using namespace readingcharts;

TEST(ReadingStatsDateLabels, FormatsAllEnglishMonthsAndInvalidDates) {
  constexpr const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char text[32];
  for (uint8_t month = 1; month <= 12; ++month) {
    formatReadingStatsMonthToken({2026, month, 1}, text, sizeof(text));
    EXPECT_STREQ(text, months[month - 1]);
  }
  formatReadingStatsShortDate({2026, 2, 3}, text, sizeof(text));
  EXPECT_STREQ(text, "Feb 3");
  formatReadingStatsMonthToken({}, text, sizeof(text));
  EXPECT_STREQ(text, "-");
}

TEST(ReadingStatsDateLabels, UsesTranslatedUtf8MonthForBothLabels) {
  testMonthTranslation = "févr.";
  char text[32];
  formatReadingStatsShortDate({2026, 2, 3}, text, sizeof(text));
  EXPECT_STREQ(text, "févr. 3");
  formatReadingStatsMonthToken({2026, 2, 3}, text, sizeof(text));
  EXPECT_STREQ(text, "févr.");
  testMonthTranslation = nullptr;
}

TEST(ReadingChartModel, BoundsBarScalingAndPreservesSmallValues) {
  EXPECT_EQ(barWidth(0, 0, 400), 0);
  EXPECT_EQ(barWidth(1, UINT32_MAX, 400), 1);
  EXPECT_EQ(barWidth(UINT32_MAX, UINT32_MAX, 400), 400);
  EXPECT_EQ(barWidth(100, 200, 400), 200);
  EXPECT_EQ(barWidth(200, 100, 400), 400);
  EXPECT_EQ(barWidth(UINT32_MAX, 1, 400), 400);
  EXPECT_EQ(barWidth(1, 1, -1), 0);
}

TEST(ReadingChartModel, EmptyAndPaddingOnlyRecordsHaveNoHistory) {
  Data data;
  EXPECT_FALSE(hasHistory(data));
  EXPECT_EQ(monthRange(data).first, -1);
  data.history.back() = 0xfc;
  EXPECT_FALSE(hasHistory(data));
  EXPECT_EQ(monthRange(data).last, -1);
}

TEST(ReadingChartModel, MonthConversionStaysWithinSupportedDates) {
  EXPECT_EQ(monthIndex({2000, 1, 1}), 0);
  EXPECT_EQ(monthIndex({2099, 12, 31}), 1199);
  EXPECT_EQ(monthIndex({2100, 1, 1}), -1);
  EXPECT_FALSE(monthDate(-1).isValid());
  EXPECT_FALSE(monthDate(1200).isValid());
  EXPECT_EQ(monthDate(25).year, 2002);
  EXPECT_EQ(monthDate(25).month, 2);
  EXPECT_EQ(daysInMonth(monthDate(289).year, monthDate(289).month), 29);
}

TEST(ReadingChartModel, RetainsTheOldestBitAndDistinguishesUnknownDays) {
  Data data;
  data.anchor = 1000;
  data.history.front() = 1;
  data.history.back() = 2;
  data.today = {};
  ReadingStatsDate day;
  ASSERT_TRUE(readingStatsDateFromDayIndex(271, day));
  EXPECT_EQ(dayState(data, day), Day::Recorded);
  ASSERT_TRUE(readingStatsDateFromDayIndex(270, day));
  EXPECT_EQ(dayState(data, day), Day::OutsideHistory);
  ASSERT_TRUE(readingStatsDateFromDayIndex(999, day));
  EXPECT_EQ(dayState(data, day), Day::Unrecorded);
  ASSERT_TRUE(readingStatsDateFromDayIndex(1000, day));
  EXPECT_EQ(dayState(data, day), Day::Recorded);
}

TEST(ReadingChartModel, AllowsNoClockHistoryAndRejectsInvalidAnchors) {
  Data data;
  data.anchor = 800;
  data.history.front() = 1;
  ReadingStatsDate anchor;
  ASSERT_TRUE(readingStatsDateFromDayIndex(800, anchor));
  EXPECT_EQ(monthRange(data).last, monthIndex(anchor));
  data.anchor = UINT32_MAX;
  EXPECT_EQ(monthRange(data).first, -1);
}

TEST(ReadingChartModel, FutureAndPostAnchorDaysUseTheKnownClockBoundary) {
  Data data;
  data.anchor = readingStatsDayIndex({2026, 11, 1});
  data.history.front() = 1;
  data.today = {2026, 11, 4};
  EXPECT_EQ(dayState(data, {2026, 11, 2}), Day::Unrecorded);
  EXPECT_EQ(dayState(data, {2026, 11, 5}), Day::OutsideHistory);
  EXPECT_EQ(dayState(data, {}), Day::OutsideHistory);
  EXPECT_EQ(monthRange(data).last, monthIndex(data.today));
  data.today = {};
  EXPECT_EQ(dayState(data, {2026, 11, 2}), Day::OutsideHistory);
}

TEST(ReadingChartModel, EpochDayZeroCanHoldHistory) {
  Data data;
  data.anchor = 0;
  data.history.front() = 1;
  EXPECT_EQ(monthRange(data).first, 0);
  EXPECT_EQ(monthRange(data).last, 0);
  EXPECT_EQ(dayState(data, {2000, 1, 1}), Day::Recorded);
}

TEST(ReadingChartModel, ClockRollbackPreservesFutureDatedStoredRecords) {
  Data data;
  data.anchor = readingStatsDayIndex({2026, 11, 10});
  data.history.front() = 5;
  data.today = {2026, 10, 4};
  EXPECT_EQ(monthRange(data).last, monthIndex({2026, 11, 10}));
  EXPECT_EQ(dayState(data, {2026, 11, 8}), Day::Recorded);
  EXPECT_EQ(dayState(data, {2026, 11, 9}), Day::Unrecorded);
  EXPECT_EQ(dayState(data, {2026, 11, 10}), Day::Recorded);
  EXPECT_EQ(dayState(data, {2026, 11, 11}), Day::OutsideHistory);
}

TEST(ReadingChartModel, CountsRecordedDaysInWindowsEndingToday) {
  std::array<uint8_t, READING_HISTORY_BYTES> bits{};
  uint32_t anchor = 0;
  for (const auto& date : {ReadingStatsDate{2026, 11, 6}, ReadingStatsDate{2026, 11, 1}, ReadingStatsDate{2026, 10, 30},
                           ReadingStatsDate{2026, 10, 8}, ReadingStatsDate{2026, 10, 7}})
    markReadingHistoryDay(anchor, bits, readingStatsDayIndex(date));
  EXPECT_EQ(recordedReadingDays(anchor, bits, {2026, 11, 6}, 7), 2);
  EXPECT_EQ(recordedReadingDays(anchor, bits, {2026, 11, 6}, 30), 4);
  EXPECT_EQ(recordedReadingDays(anchor, bits, {2026, 11, 6}, 0), 0);
  EXPECT_EQ(recordedReadingDays(anchor, bits, {}, 30), 0);
}

TEST(ReadingChartModel, CountsIgnoreFutureDaysAndHandleEpochAndOldHistory) {
  std::array<uint8_t, READING_HISTORY_BYTES> bits{};
  uint32_t anchor = 0;
  markReadingHistoryDay(anchor, bits, readingStatsDayIndex({2000, 1, 1}));
  markReadingHistoryDay(anchor, bits, readingStatsDayIndex({2000, 1, 3}));
  EXPECT_EQ(recordedReadingDays(anchor, bits, {2000, 1, 2}, 30), 1);
  EXPECT_EQ(recordedReadingDays(anchor, bits, {2000, 1, 3}, 30), 2);
  EXPECT_EQ(recordedReadingDays(anchor, bits, {2003, 1, 1}, 30), 0);
  EXPECT_EQ(recordedReadingDays(UINT32_MAX, bits, {2000, 1, 3}, 30), 0);
}

TEST(ReadingChartModel, FutureAnchorMustCoverTheWholeWindow) {
  const ReadingStatsDate today{2026, 11, 6};
  const uint32_t current = readingStatsDayIndex(today);
  EXPECT_TRUE(readingHistoryCoversWindow(current + 700, today, 30));
  EXPECT_FALSE(readingHistoryCoversWindow(current + 701, today, 30));
  EXPECT_TRUE(readingHistoryCoversWindow(current + 723, today, 7));
  EXPECT_FALSE(readingHistoryCoversWindow(current + 724, today, 7));
  EXPECT_FALSE(readingHistoryCoversWindow(UINT32_MAX, today, 7));
  EXPECT_FALSE(readingHistoryCoversWindow(current, {}, 7));
  EXPECT_TRUE(readingHistoryCoversWindow(729, {2000, 1, 1}, 30));
  EXPECT_FALSE(readingHistoryCoversWindow(730, {2000, 1, 1}, 30));
}

TEST(ReadingChartModel, RecordedYearIncludesTodayAndPrior364Days) {
  const ReadingStatsDate today{2026, 11, 6};
  const uint32_t current = readingStatsDayIndex(today);
  std::array<uint8_t, READING_HISTORY_BYTES> bits{};
  uint32_t anchor = 0;
  markReadingHistoryDay(anchor, bits, current - 365);
  markReadingHistoryDay(anchor, bits, current - 364);
  markReadingHistoryDay(anchor, bits, current);
  EXPECT_EQ(recordedReadingDays(anchor, bits, today, 365), 2);
  EXPECT_TRUE(readingHistoryCoversWindow(current + 365, today, 365));
  EXPECT_FALSE(readingHistoryCoversWindow(current + 366, today, 365));
}

TEST(ReadingChartModel, AggregatedRecordedDaysCountOverlappingDaysOnce) {
  const ReadingStatsDate today{2026, 11, 6};
  const uint32_t current = readingStatsDayIndex(today);
  std::array<uint8_t, READING_HISTORY_BYTES> local{}, remote{};
  uint32_t localAnchor = 0, remoteAnchor = 0;
  markReadingHistoryDay(localAnchor, local, current - 2);
  markReadingHistoryDay(localAnchor, local, current);
  markReadingHistoryDay(remoteAnchor, remote, current - 40);
  markReadingHistoryDay(remoteAnchor, remote, current);
  mergeReadingHistory(localAnchor, local, remoteAnchor, remote);
  EXPECT_EQ(recordedReadingDays(localAnchor, local, today, 7), 2);
  EXPECT_EQ(recordedReadingDays(localAnchor, local, today, 30), 2);
  EXPECT_EQ(recordedReadingDays(localAnchor, local, today, 365), 3);
}
