#include <ReadingStatsSession.h>
#include <gtest/gtest.h>

namespace {
struct StatsRun {
  BookReadingStats book;
  GlobalReadingStats global;
  ReadingStatsSession session;
  void render(uint32_t ms) { session.onPageRendered(ms, nullptr, ReadingStatsFrame::Page, book, global, 300); }
  void pause(uint32_t ms) { session.pause(ms, book, global, 300); }
  void finish() { session.commit(book, global); }
};
}  // namespace

TEST(ReadingStatsSessionTest, CountsWithoutRtcAndQualifiesOneSession) {
  StatsRun r;
  r.render(0);
  r.session.noteTurn(true, true, 65000);
  r.render(65000);
  r.pause(65000);
  r.finish();
  EXPECT_EQ(r.book.totalReadingSeconds, 65U);
  EXPECT_EQ(r.global.totalReadingSeconds, 65U);
  EXPECT_EQ(r.book.sessionCount, 1U);
  EXPECT_EQ(r.global.totalPagesTurned, 1U);
  EXPECT_EQ(r.book.avgSecondsPerForwardPage, 65U);
  EXPECT_FALSE(r.book.startDate.isValid());
}

TEST(ReadingStatsSessionTest, RedrawDoesNotDiscardCurrentDwell) {
  StatsRun r;
  r.render(0);
  r.render(5000);
  r.session.noteTurn(true, true, 15000);
  r.render(15000);
  r.finish();
  EXPECT_EQ(r.book.totalReadingSeconds, 15U);
  EXPECT_EQ(r.book.totalPagesTurned, 1U);
}

TEST(ReadingStatsSessionTest, ExcludesIdleAndMenusButCountsBackwardDwell) {
  StatsRun r;
  r.render(0);
  r.pause(20000);
  r.render(600000);
  r.session.noteTurn(false, true, 615000);
  r.render(615000);
  r.session.noteTurn(true, true, 1000000);
  r.render(1000000);
  r.finish();
  EXPECT_EQ(r.book.totalReadingSeconds, 35U);
  EXPECT_EQ(r.book.totalPagesTurned, 0U);
  EXPECT_EQ(r.book.sessionCount, 0U);
}

TEST(ReadingStatsSessionTest, IgnoresFailedTurnsAndHandlesMillisWrap) {
  StatsRun r;
  r.render(UINT32_MAX - 4999);
  r.session.noteTurn(true, false, 5000);
  r.render(5000);
  r.session.noteTurn(true, true, 10000);
  r.render(10000);
  r.finish();
  EXPECT_EQ(r.book.totalReadingSeconds, 15U);
  EXPECT_EQ(r.book.totalPagesTurned, 1U);
}

TEST(ReadingStatsSessionTest, FastBrowsingDoesNotAddPaceOrReadingTime) {
  StatsRun r;
  r.render(0);
  r.session.noteTurn(true, true, 500);
  r.render(500);
  r.pause(500);
  r.finish();
  EXPECT_EQ(r.book.totalReadingSeconds, 0U);
  EXPECT_EQ(r.book.totalPagesTurned, 0U);
  EXPECT_EQ(r.book.paceSampleCount, 0U);
}

TEST(ReadingStatsSessionTest, CountersSaturateInsteadOfWrapping) {
  StatsRun r;
  r.book.totalReadingSeconds = UINT32_MAX - 1;
  r.book.sessionCount = UINT16_MAX;
  r.global.totalReadingSeconds = UINT32_MAX;
  r.global.totalSessions = UINT32_MAX;
  r.render(0);
  r.pause(65000);
  r.finish();
  EXPECT_EQ(r.book.totalReadingSeconds, UINT32_MAX);
  EXPECT_EQ(r.book.sessionCount, UINT16_MAX);
  EXPECT_EQ(r.global.totalReadingSeconds, UINT32_MAX);
  EXPECT_EQ(r.global.totalSessions, UINT32_MAX);
}

TEST(ReadingStatsSessionTest, AttributesResumedReadingToActualDayWithoutCountingMenuTime) {
  StatsRun r;
  const ReadingStatsDateTime evening{{2026, 11, 3}, 23, 50, 0};
  const ReadingStatsDateTime morning{{2026, 11, 4}, 9, 0, 0};
  r.session.onPageRendered(0, &evening, ReadingStatsFrame::Page, r.book, r.global, 300);
  r.pause(60000);
  r.session.onPageRendered(3600000, &morning, ReadingStatsFrame::Page, r.book, r.global, 300);
  r.pause(3720000);
  r.finish();
  EXPECT_EQ(r.book.totalReadingSeconds, 180U);
  EXPECT_EQ(r.book.dayOfWeekSeconds[1], 60U);
  EXPECT_EQ(r.book.dayOfWeekSeconds[2], 120U);
  EXPECT_EQ(r.book.timeOfDaySeconds[static_cast<size_t>(ReadingTimeBucket::Night)], 60U);
  EXPECT_EQ(r.book.timeOfDaySeconds[static_cast<size_t>(ReadingTimeBucket::Morning)], 120U);
  EXPECT_EQ(r.global.currentReadingStreak(&morning.date), 2U);
  EXPECT_EQ(r.book.sessionCount, 1U);
}

TEST(ReadingStatsSessionTest, DoesNotCountDisplayRefreshAsReading) {
  StatsRun r;
  r.render(0);
  r.session.noteTurn(true, true, 10000);
  r.render(12500);
  r.pause(22500);
  r.finish();
  EXPECT_EQ(r.book.totalReadingSeconds, 20U);
  EXPECT_EQ(r.book.avgSecondsPerForwardPage, 10U);
}

TEST(ReadingStatsSessionTest, FinalTurnCountsPaceWithoutEndScreenRefresh) {
  StatsRun r;
  r.render(0);
  r.session.noteTurn(true, true, 15000);
  r.session.onPageRendered(18000, nullptr, ReadingStatsFrame::EndOfBook, r.book, r.global, 300);
  r.finish();
  EXPECT_EQ(r.book.totalReadingSeconds, 15U);
  EXPECT_EQ(r.book.totalPagesTurned, 1U);
  EXPECT_EQ(r.book.avgSecondsPerForwardPage, 15U);
}

TEST(ReadingStatsSessionTest, RepeatedSamePageRefreshKeepsDwellAndDoesNotCountPages) {
  StatsRun r;
  r.render(0);
  r.render(5000);
  r.render(10000);
  r.pause(20000);
  r.finish();
  EXPECT_EQ(r.book.totalReadingSeconds, 20U);
  EXPECT_EQ(r.book.totalPagesTurned, 0U);
}

TEST(ReadingStatsSessionTest, RepeatedPreviewDoesNotCommitOrDrainTheSession) {
  StatsRun r;
  const ReadingStatsDateTime date{{2026, 11, 15}, 12, 0, 0};
  r.session.onPageRendered(0, &date, ReadingStatsFrame::Page, r.book, r.global, 300);
  r.pause(65000);
  BookReadingStats first = r.book, second = r.book;
  GlobalReadingStats firstGlobal = r.global, secondGlobal = r.global;
  r.session.preview(first, firstGlobal);
  r.session.preview(second, secondGlobal);
  EXPECT_EQ(first.totalReadingSeconds, 65U);
  EXPECT_EQ(second.totalReadingSeconds, 65U);
  EXPECT_EQ(first.timeOfDaySeconds, second.timeOfDaySeconds);
  EXPECT_EQ(firstGlobal.readingHistoryBits, secondGlobal.readingHistoryBits);
  EXPECT_EQ(r.book.totalReadingSeconds, 0U);
  EXPECT_EQ(r.session.pendingSeconds(), 65U);
  r.finish();
  EXPECT_EQ(r.book.totalReadingSeconds, 65U);
  EXPECT_EQ(r.global.totalReadingSeconds, 65U);
  EXPECT_EQ(r.book.timeOfDaySeconds, first.timeOfDaySeconds);
}
