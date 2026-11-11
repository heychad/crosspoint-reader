#include <Arduino.h>
#include <BookTracking.h>
#include <HalStorage.h>
#include <ReaderStats.h>
#include <ReadingStatsStore.h>
#include <gtest/gtest.h>

namespace {
const std::string RESET_BOOK = "/Books/reset.xtc";
const std::string RESET_CACHE = "/.crosspoint/xtc_" + std::to_string(std::hash<std::string>{}(RESET_BOOK));
void createResetBook() {
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", RESET_BOOK, file));
  ASSERT_TRUE(file.close());
}
}  // namespace

TEST(ReaderStatsReset, FlushesThenResumesWithoutRestoringOldBookCounters) {
  Storage.reset();
  createResetBook();
  testMillis = 0;
  ReaderStats stats;
  stats.begin(RESET_CACHE, RESET_BOOK, true, 300);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 65000;
  ASSERT_EQ(stats.resetBookStatistics(RESET_CACHE, RESET_BOOK, true, 300), statsbackup::BookRestoreResult::Restored);
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(RESET_CACHE)).totalReadingSeconds, 0U);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 85000;
  ASSERT_TRUE(stats.finish());
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(RESET_CACHE)).totalReadingSeconds, 20U);
  EXPECT_EQ(GlobalReadingStats::load().totalReadingSeconds, 85U);
}

TEST(ReaderStatsReset, FreshBookWithNoSavedHistoryIsAlreadyReset) {
  Storage.reset();
  createResetBook();
  testMillis = 0;
  ReaderStats stats;
  stats.begin(RESET_CACHE, RESET_BOOK, true, 300);
  ASSERT_EQ(stats.resetBookStatistics(RESET_CACHE, RESET_BOOK, true, 300), statsbackup::BookRestoreResult::Restored);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 65000;
  ASSERT_TRUE(stats.finish());
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(RESET_CACHE)).totalReadingSeconds, 65U);
}

TEST(ReaderStatsReset, ResumingDoesNotRestoreClearedCompletion) {
  Storage.reset();
  createResetBook();
  BookReadingStats previous;
  previous.isCompleted = true;
  previous.totalReadingSeconds = 100;
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(RESET_CACHE), previous));
  GlobalReadingStats global;
  global.completedBooks = 1;
  global.totalReadingSeconds = 100;
  ASSERT_TRUE(global.save());
  testMillis = 0;
  ReaderStats stats;
  stats.begin(RESET_CACHE, RESET_BOOK, true, 300, true);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 65000;
  ASSERT_EQ(stats.resetBookStatistics(RESET_CACHE, RESET_BOOK, true, 300, true),
            statsbackup::BookRestoreResult::Restored);
  bool completed = true;
  ASSERT_TRUE(stats.completionStatus(completed));
  EXPECT_FALSE(completed);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 85000;
  ASSERT_TRUE(stats.finish());
  const auto current = BookReadingStats::load(readingstats::bookDirectory(RESET_CACHE));
  EXPECT_FALSE(current.isCompleted);
  EXPECT_EQ(current.totalReadingSeconds, 20U);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().completedBooks, 0U);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().totalReadingSeconds, 185U);
}

TEST(ReaderStatsReset, FailedFlushLeavesSessionAvailableForRetry) {
  Storage.reset();
  createResetBook();
  testMillis = 0;
  ReaderStats stats;
  stats.begin(RESET_CACHE, RESET_BOOK, true, 300);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 65000;
  Storage.failNextRenameFrom("/.crosspoint/global_stats.bin.tmp");
  EXPECT_EQ(stats.resetBookStatistics(RESET_CACHE, RESET_BOOK, true, 300), statsbackup::BookRestoreResult::Unavailable);
  ASSERT_TRUE(stats.finish());
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(RESET_CACHE)).totalReadingSeconds, 65U);
}

TEST(ReaderStatsReset, UncertainResetStopsOldSessionFromOverwritingRecoveryFiles) {
  Storage.reset();
  createResetBook();
  testMillis = 0;
  ReaderStats stats;
  stats.begin(RESET_CACHE, RESET_BOOK, true, 300);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 65000;
  ASSERT_TRUE(stats.finish());
  const auto primary = readingstats::bookDirectory(RESET_CACHE) + "/stats_v5.bin";
  Storage.failNextRenameFrom(primary + ".tmp");
  EXPECT_EQ(stats.resetBookStatistics(RESET_CACHE, RESET_BOOK, true, 300),
            statsbackup::BookRestoreResult::RecoveryRequired);
  EXPECT_TRUE(stats.unavailableAtStart());
  EXPECT_FALSE(StatsSessionLease::anyActive());
  BookReadingStats book;
  GlobalReadingStats global;
  bool saved = false;
  EXPECT_FALSE(stats.snapshot(book, global, saved));
  EXPECT_TRUE(stats.finish());
  EXPECT_TRUE(Storage.exists((primary + ".tmp").c_str()));
}

TEST(ReaderStatsReset, KeepsDisabledTrackingDisabled) {
  Storage.reset();
  createResetBook();
  BookReadingStats previous;
  previous.totalReadingSeconds = 100;
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(RESET_CACHE), previous));
  ASSERT_TRUE(booktracking::setEnabled(RESET_CACHE, RESET_BOOK, false));
  ReaderStats stats;
  stats.begin(RESET_CACHE, RESET_BOOK, true, 300);
  ASSERT_EQ(stats.resetBookStatistics(RESET_CACHE, RESET_BOOK, true, 300), statsbackup::BookRestoreResult::Restored);
  EXPECT_EQ(booktracking::state(RESET_CACHE, RESET_BOOK), booktracking::State::Disabled);
  EXPECT_FALSE(StatsSessionLease::anyActive());
  BookReadingStats book;
  GlobalReadingStats global;
  bool saved = false;
  EXPECT_FALSE(stats.snapshot(book, global, saved));
}

TEST(ReaderStatsTracking, ToggleOffAndOnCountsOnlyEnabledIntervals) {
  Storage.reset();
  testMillis = 0;
  ReaderStats stats;
  const std::string cache = "/.crosspoint/xtc_123", path = "/Books/test.xtc";
  stats.begin(cache, path, true, 300);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 65000;
  ASSERT_TRUE(booktracking::setEnabled(cache, path, false));
  ASSERT_TRUE(stats.refreshTracking(cache, path, true, 300));
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 130000;
  ASSERT_TRUE(stats.finish());
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(cache)).totalReadingSeconds, 65U);
  ASSERT_TRUE(booktracking::setEnabled(cache, path, true));
  ASSERT_TRUE(stats.refreshTracking(cache, path, true, 300));
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 150000;
  ASSERT_TRUE(stats.refreshTracking(cache, path, true, 300));
  testMillis = 195000;
  ASSERT_TRUE(stats.finish());
  const auto saved = BookReadingStats::load(readingstats::bookDirectory(cache));
  EXPECT_EQ(saved.totalReadingSeconds, 130U);
  EXPECT_EQ(saved.sessionCount, 2U);
  EXPECT_EQ(GlobalReadingStats::load().totalReadingSeconds, 130U);
}

TEST(ReaderStatsTracking, FailedFlushKeepsTimeForRetry) {
  for (const bool failBook : {true, false}) {
    Storage.reset();
    testMillis = 0;
    ReaderStats stats;
    const std::string cache = "/.crosspoint/xtc_123", path = "/Books/test.xtc";
    stats.begin(cache, path, true, 300);
    stats.onRender(ReadingStatsFrame::Page);
    testMillis = 65000;
    ASSERT_TRUE(booktracking::setEnabled(cache, path, false));
    Storage.failNextRenameFrom(failBook ? readingstats::bookDirectory(cache) + "/stats_v5.bin.tmp"
                                        : "/.crosspoint/global_stats.bin.tmp");
    EXPECT_FALSE(stats.refreshTracking(cache, path, true, 300));
    ASSERT_TRUE(stats.refreshTracking(cache, path, true, 300));
    EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(cache)).totalReadingSeconds, 65U);
    EXPECT_EQ(GlobalReadingStats::load().totalReadingSeconds, 65U);
  }
}

TEST(ReaderStatsTracking, UnknownStateAndMismatchedCachePreserveLiveSession) {
  Storage.reset();
  testMillis = 0;
  ReaderStats stats;
  const std::string cache = "/.crosspoint/xtc_123", path = "/Books/test.xtc";
  stats.begin(cache, path, true, 300);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 30000;
  EXPECT_FALSE(stats.refreshTracking("/.crosspoint/xtc_other", path, true, 300));
  Storage.failReadsFor(readingstats::bookDirectory(cache) + "/reading_stats_off");
  EXPECT_FALSE(stats.refreshTracking(cache, path, true, 300));
  testMillis = 65000;
  ASSERT_TRUE(stats.finish());
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(cache)).totalReadingSeconds, 65U);
}

TEST(ReaderStatsTracking, GlobalTrackingOffPreventsPerBookEnable) {
  Storage.reset();
  testMillis = 0;
  ReaderStats stats;
  const std::string cache = "/.crosspoint/xtc_123", path = "/Books/test.xtc";
  stats.begin(cache, path, false, 300);
  ASSERT_TRUE(stats.refreshTracking(cache, path, false, 300));
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 65000;
  ASSERT_TRUE(stats.finish());
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(cache)).totalReadingSeconds, 0U);
  ASSERT_TRUE(stats.refreshTracking(cache, path, true, 300));
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 130000;
  ASSERT_TRUE(stats.finish());
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(cache)).totalReadingSeconds, 65U);
}

TEST(ReaderStatsFinish, ReportsSaveFailureAndRetriesWithoutDuplicatingTime) {
  for (const bool failBook : {true, false}) {
    SCOPED_TRACE(failBook ? "book save" : "global save");
    Storage.reset();
    testMillis = 0;
    ReaderStats stats;
    const std::string cache = "/.crosspoint/epub_123";
    const std::string book = "/Books/test.epub";
    stats.begin(cache, book, true, 300);
    stats.onRender(ReadingStatsFrame::Page);
    testMillis = 65000;
    stats.noteTurn(true, true);
    stats.onRender(ReadingStatsFrame::Page);
    const auto directory = readingstats::bookDirectory(cache);
    Storage.failNextRenameFrom(failBook ? directory + "/stats_v5.bin.tmp" : "/.crosspoint/global_stats.bin.tmp");
    EXPECT_FALSE(stats.finish());
    EXPECT_TRUE(stats.finish());
    bool enabled = false;
    const auto saved = readingstats::loadBook(cache, book, enabled);
    EXPECT_TRUE(enabled);
    EXPECT_EQ(saved.totalReadingSeconds, 65U);
    EXPECT_EQ(saved.sessionCount, 1U);
    EXPECT_EQ(GlobalReadingStats::load().totalReadingSeconds, 65U);
  }
}

TEST(ReaderStatsFinish, StatisticsSnapshotsPreserveOneSessionAcrossRepeatedViews) {
  Storage.reset();
  testMillis = 0;
  ReaderStats stats;
  const std::string cache = "/.crosspoint/epub_123";
  stats.begin(cache, "/Books/test.epub", true, 300);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 65000;
  BookReadingStats book;
  GlobalReadingStats global;
  bool saved = false;
  ASSERT_TRUE(stats.snapshot(book, global, saved));
  ASSERT_TRUE(saved);
  EXPECT_EQ(book.totalReadingSeconds, 65U);
  EXPECT_EQ(book.sessionCount, 1U);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 130000;
  ASSERT_TRUE(stats.snapshot(book, global, saved));
  ASSERT_TRUE(saved);
  EXPECT_EQ(book.totalReadingSeconds, 130U);
  EXPECT_EQ(book.sessionCount, 1U);
  EXPECT_EQ(global.totalSessions, 1U);
  ASSERT_TRUE(stats.finish());
  bool enabled = false;
  const auto persisted = readingstats::loadBook(cache, "/Books/test.epub", enabled);
  EXPECT_EQ(persisted.totalReadingSeconds, 130U);
  EXPECT_EQ(persisted.sessionCount, 1U);
  EXPECT_EQ(GlobalReadingStats::load().totalSessions, 1U);
}

TEST(ReaderStatsFinish, StatisticsSnapshotDoesNotDiscardShortReadingIntervals) {
  Storage.reset();
  testMillis = 0;
  ReaderStats stats;
  const std::string cache = "/.crosspoint/epub_123";
  stats.begin(cache, "/Books/test.epub", true, 300);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 5000;
  BookReadingStats book;
  GlobalReadingStats global;
  bool saved = false;
  ASSERT_TRUE(stats.snapshot(book, global, saved));
  EXPECT_TRUE(saved);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 12000;
  ASSERT_TRUE(stats.finish());
  bool enabled = false;
  EXPECT_EQ(readingstats::loadBook(cache, "/Books/test.epub", enabled).totalReadingSeconds, 12U);
}

TEST(ReaderStatsFinish, FailedSnapshotIsRetriedAtExit) {
  for (const bool failBook : {true, false}) {
    SCOPED_TRACE(failBook ? "book save" : "global save");
    Storage.reset();
    testMillis = 0;
    ReaderStats stats;
    const std::string cache = "/.crosspoint/epub_123";
    stats.begin(cache, "/Books/test.epub", true, 300);
    stats.onRender(ReadingStatsFrame::Page);
    testMillis = 65000;
    BookReadingStats book;
    GlobalReadingStats global;
    bool saved = true;
    Storage.failNextRenameFrom(failBook ? readingstats::bookDirectory(cache) + "/stats_v5.bin.tmp"
                                        : "/.crosspoint/global_stats.bin.tmp");
    ASSERT_TRUE(stats.snapshot(book, global, saved));
    EXPECT_FALSE(saved);
    EXPECT_EQ(book.totalReadingSeconds, 65U);
    ASSERT_TRUE(stats.finish());
    bool enabled = false;
    const auto persisted = readingstats::loadBook(cache, "/Books/test.epub", enabled);
    EXPECT_EQ(persisted.totalReadingSeconds, 65U);
    EXPECT_EQ(persisted.sessionCount, 1U);
    EXPECT_EQ(GlobalReadingStats::load().totalReadingSeconds, 65U);
  }
}

TEST(ReaderStatsFinish, TimeLeftUsesSavedAndCurrentReadingWithoutEndingSession) {
  Storage.reset();
  testMillis = 0;
  const std::string cache = "/.crosspoint/epub_123";
  BookReadingStats saved;
  saved.totalReadingSeconds = 120;
  saved.avgSecondsPerForwardPage = 30;
  saved.paceSampleCount = 1;
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(cache), saved));
  ReaderStats stats;
  stats.begin(cache, "/Books/test.epub", true, 300);
  EXPECT_EQ(stats.bookTimeLeft(5000), 120U);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 65000;
  stats.noteTurn(true, true);
  // The reader paints its bar before recording the turn. Previewing must
  // include it without committing or double-counting the session.
  EXPECT_EQ(stats.bookTimeLeft(5000), 185U);
  EXPECT_EQ(stats.bookTimeLeft(5000), 185U);
  stats.onRender(ReadingStatsFrame::Page);
  EXPECT_EQ(stats.bookTimeLeft(5000), 185U);
  EXPECT_EQ(stats.bookTimeLeft(0), 0U);
  ASSERT_TRUE(stats.finish());
  bool enabled = false;
  EXPECT_EQ(readingstats::loadBook(cache, "/Books/test.epub", enabled).totalReadingSeconds, 185U);
}

TEST(ReaderStatsFinish, TimeLeftUnavailableWithoutTrackingOrSufficientHistory) {
  Storage.reset();
  testMillis = 0;
  ReaderStats disabled;
  disabled.begin("/.crosspoint/epub_123", "/Books/test.epub", false, 300);
  EXPECT_EQ(disabled.bookTimeLeft(5000), 0U);
  ReaderStats fresh;
  fresh.begin("/.crosspoint/epub_123", "/Books/test.epub", true, 300);
  EXPECT_EQ(fresh.bookTimeLeft(5000), 0U);
}

TEST(ReaderStatsFinish, SectionTimeUsesRecordedPagePaceAndProtectsBounds) {
  Storage.reset();
  testMillis = 0;
  const std::string cache = "/.crosspoint/epub_123";
  BookReadingStats saved;
  saved.avgSecondsPerForwardPage = 30;
  saved.paceSampleCount = 1;
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(cache), saved));
  ReaderStats stats;
  stats.begin(cache, "/Books/test.epub", true, 300);
  EXPECT_EQ(stats.sectionTimeLeft(1, 10), 270U);
  EXPECT_EQ(stats.sectionTimeLeft(9, 10), 30U);
  EXPECT_EQ(stats.sectionTimeLeft(10, 10), 0U);
  EXPECT_EQ(stats.sectionTimeLeft(0, 10), 0U);
  EXPECT_EQ(stats.sectionTimeLeft(11, 10), 0U);
  EXPECT_EQ(stats.sectionTimeLeft(1, INT32_MAX), UINT32_MAX);
  ReaderStats disabled;
  disabled.begin(cache, "/Books/test.epub", false, 300);
  EXPECT_EQ(disabled.sectionTimeLeft(1, 10), 0U);
}

TEST(ReaderStatsFinish, SectionTimeIsUnavailableUntilPaceIsRecorded) {
  Storage.reset();
  testMillis = 0;
  ReaderStats stats;
  stats.begin("/.crosspoint/epub_123", "/Books/test.epub", true, 300);
  EXPECT_EQ(stats.sectionTimeLeft(1, 10), 0U);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 30000;
  stats.noteTurn(true, true);
  // The bar paints before the page render commits this pending sample.
  EXPECT_EQ(stats.sectionTimeLeft(2, 10), 240U);
  EXPECT_EQ(stats.sectionTimeLeft(2, 10), 240U);
  stats.onRender(ReadingStatsFrame::Page);
  EXPECT_EQ(stats.sectionTimeLeft(2, 10), 240U);
  ASSERT_TRUE(stats.finish());
  bool enabled = false;
  const auto book = readingstats::loadBook("/.crosspoint/epub_123", "/Books/test.epub", enabled);
  EXPECT_EQ(book.paceSampleCount, 1U);
  EXPECT_EQ(book.avgSecondsPerForwardPage, 30U);
}

TEST(ReaderStatsFinish, SectionTimePreviewIgnoresFailedBackwardAndIdleTurns) {
  for (int kind = 0; kind < 3; ++kind) {
    Storage.reset();
    testMillis = 0;
    ReaderStats stats;
    stats.begin("/.crosspoint/epub_123", "/Books/test.epub", true, 30);
    stats.onRender(ReadingStatsFrame::Page);
    testMillis = kind == 2 ? 31000 : 10000;
    stats.noteTurn(kind != 1, kind != 0);
    EXPECT_EQ(stats.sectionTimeLeft(2, 10), 0U);
  }
}

TEST(ReaderStatsFinish, SectionTimePreviewIncludesLatestPaceWithoutDoubleCounting) {
  Storage.reset();
  testMillis = 0;
  ReaderStats stats;
  stats.begin("/.crosspoint/epub_123", "/Books/test.epub", true, 300);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 30000;
  stats.noteTurn(true, true);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 90000;
  stats.noteTurn(true, true);
  EXPECT_EQ(stats.sectionTimeLeft(3, 10), 315U);
  EXPECT_EQ(stats.sectionTimeLeft(3, 10), 315U);
  stats.onRender(ReadingStatsFrame::Page);
  EXPECT_EQ(stats.sectionTimeLeft(3, 10), 315U);
  ASSERT_TRUE(stats.finish());
  bool enabled = false;
  const auto book = readingstats::loadBook("/.crosspoint/epub_123", "/Books/test.epub", enabled);
  EXPECT_EQ(book.paceSampleCount, 2U);
  EXPECT_EQ(book.avgSecondsPerForwardPage, 45U);
}

TEST(ReaderStatsFinish, TimeLeftThresholdIncludesTurnBeforePageIsPainted) {
  Storage.reset();
  testMillis = 0;
  const std::string cache = "/.crosspoint/epub_123";
  BookReadingStats saved;
  saved.totalReadingSeconds = 110;
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(cache), saved));
  ReaderStats stats;
  stats.begin(cache, "/Books/test.epub", true, 300);
  stats.onRender(ReadingStatsFrame::Page);
  EXPECT_EQ(stats.bookTimeLeft(5000), 0U);
  testMillis = 10000;
  stats.noteTurn(true, true);
  EXPECT_EQ(stats.bookTimeLeft(5000), 120U);
  stats.onRender(ReadingStatsFrame::Page);
  EXPECT_EQ(stats.bookTimeLeft(5000), 120U);
  ASSERT_TRUE(stats.finish());
  bool enabled = false;
  EXPECT_EQ(readingstats::loadBook(cache, "/Books/test.epub", enabled).totalReadingSeconds, 120U);
}

TEST(ReaderStatsFinish, TimeLeftPreviewMatchesFailedAndIdleTurnPolicy) {
  for (const bool turnSucceeds : {false, true}) {
    Storage.reset();
    testMillis = 0;
    const std::string cache = "/.crosspoint/epub_123";
    BookReadingStats saved;
    saved.totalReadingSeconds = 110;
    ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(cache), saved));
    ReaderStats stats;
    stats.begin(cache, "/Books/test.epub", true, 30);
    stats.onRender(ReadingStatsFrame::Page);
    testMillis = turnSucceeds ? 31000 : 10000;
    stats.noteTurn(true, turnSucceeds);
    EXPECT_EQ(stats.bookTimeLeft(5000), 0U);
    if (turnSucceeds) {
      stats.onRender(ReadingStatsFrame::Page);
      EXPECT_EQ(stats.bookTimeLeft(5000), 0U);
    }
  }
}

TEST(ReaderStatsFinish, TimeLeftSaturatesAndCompletedBooksHaveNoEstimate) {
  Storage.reset();
  testMillis = 0;
  const std::string cache = "/.crosspoint/epub_123";
  BookReadingStats saved;
  saved.totalReadingSeconds = UINT32_MAX;
  saved.avgSecondsPerForwardPage = UINT16_MAX;
  saved.paceSampleCount = 1;
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(cache), saved));
  ReaderStats stats;
  stats.begin(cache, "/Books/test.epub", true, 300);
  EXPECT_EQ(stats.bookTimeLeft(1), UINT32_MAX);
  saved.isCompleted = true;
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(cache), saved));
  ReaderStats finished;
  finished.begin(cache, "/Books/test.epub", true, 300);
  EXPECT_EQ(finished.bookTimeLeft(5000), 0U);
}
