#include <Arduino.h>
#include <HalStorage.h>
#include <Memory.h>
#include <ReaderStats.h>
#include <ReadingStatsDateEdit.h>
#include <ReadingStatsStore.h>
#include <gtest/gtest.h>

TEST(ReadingStatsDateEdit, ClosedBookEditPreservesStatsAndRejectsStaleSnapshot) {
  Storage.reset();
  const std::string cache = "/.crosspoint/epub_123";
  const std::string path = "/Books/test.epub";
  const auto directory = readingstats::bookDirectory(cache);
  BookReadingStats original;
  original.totalReadingSeconds = 1240;
  original.totalPagesTurned = 40;
  ASSERT_TRUE(readingstats::saveBook(directory, original));
  bool enabled = false, found = false, known = false;
  auto snapshot = readingstats::loadBook(cache, path, enabled, &found, BookReadingStats::LoadMode::ReadOnly, &known);
  ASSERT_TRUE(found && known && snapshot.writable);
  ASSERT_TRUE(readingstats::editClosedBookDate(cache, path, true, {2024, 2, 29}, snapshot));
  EXPECT_TRUE(snapshot.startDateManual);
  EXPECT_EQ(snapshot.startDate.day, 29);
  auto persisted = BookReadingStats::load(directory);
  EXPECT_EQ(persisted.totalReadingSeconds, 1240U);
  EXPECT_EQ(persisted.totalPagesTurned, 40U);
  EXPECT_EQ(persisted.startDate.year, 2024);
  persisted.totalPagesTurned = 41;
  ASSERT_TRUE(readingstats::saveBook(directory, persisted));
  EXPECT_FALSE(readingstats::editClosedBookDate(cache, path, false, {2025, 1, 1}, snapshot));
  EXPECT_EQ(BookReadingStats::load(directory).totalPagesTurned, 41U);
  EXPECT_FALSE(BookReadingStats::load(directory).finishedDateManual);
}

TEST(ReadingStatsDateEdit, ClosedBookEditPreservesMissingAndUnreadableStats) {
  Storage.reset();
  const std::string cache = "/.crosspoint/epub_123";
  const std::string path = "/Books/test.epub";
  BookReadingStats snapshot;
  EXPECT_FALSE(readingstats::editClosedBookDate(cache, path, true, {2024, 1, 1}, snapshot));
  EXPECT_FALSE(Storage.exists((readingstats::bookDirectory(cache) + "/stats_v5.bin").c_str()));
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(cache), snapshot));
  Storage.setMonitorHealthy(false);
  EXPECT_FALSE(readingstats::editClosedBookDate(cache, path, true, {2024, 1, 1}, snapshot));
  Storage.setMonitorHealthy(true);
  EXPECT_FALSE(BookReadingStats::load(readingstats::bookDirectory(cache)).startDateManual);
  failNextBackupAllocation = true;
  EXPECT_FALSE(readingstats::editClosedBookDate(cache, path, true, {2024, 1, 1}, snapshot));
  EXPECT_FALSE(readingstats::editClosedBookDate(cache, path, true, {2024, 2, 30}, snapshot));
  Storage.failReadsFor(readingstats::bookDirectory(cache) + "/stats_v5.bin");
  EXPECT_FALSE(readingstats::editClosedBookDate(cache, path, true, {2024, 1, 1}, snapshot));
}

TEST(ReadingStatsDateEdit, ClosedBookWriteFailureDoesNotChangeSnapshotOrLoseOldStats) {
  Storage.reset();
  const std::string cache = "/.crosspoint/epub_123";
  const std::string path = "/Books/test.epub";
  const auto directory = readingstats::bookDirectory(cache);
  BookReadingStats snapshot;
  snapshot.totalReadingSeconds = 500;
  ASSERT_TRUE(readingstats::saveBook(directory, snapshot));
  Storage.failNextRenameFrom(directory + "/stats_v5.bin.tmp");
  EXPECT_FALSE(readingstats::editClosedBookDate(cache, path, true, {2024, 1, 1}, snapshot));
  EXPECT_FALSE(snapshot.startDateManual);
  EXPECT_EQ(BookReadingStats::load(directory).totalReadingSeconds, 500U);
}

TEST(ReadingStatsDateEdit, StrictDatesAndExplicitClear) {
  ReadingStatsDate date{2020, 1, 2};
  ASSERT_TRUE(parseReadingStatsDateEdit("2024-02-29", date));
  EXPECT_EQ(date.year, 2024);
  EXPECT_EQ(date.month, 2);
  EXPECT_EQ(date.day, 29);
  for (const char* bad : {"2023-02-29", "2100-01-01", "2024-2-29", "2024/02/29", "2024-01-0x", "2024-00-01"}) {
    EXPECT_FALSE(parseReadingStatsDateEdit(bad, date)) << bad;
    EXPECT_EQ(date.year, 2024);
    EXPECT_EQ(date.month, 2);
    EXPECT_EQ(date.day, 29);
  }
  ASSERT_TRUE(parseReadingStatsDateEdit("", date));
  EXPECT_EQ(date.year, 0);
  EXPECT_EQ(date.month, 0);
  EXPECT_EQ(date.day, 0);
}

TEST(ReadingStatsDateEdit, PreservesSessionAndPersistsOverrides) {
  Storage.reset();
  testMillis = 0;
  const std::string cache = "/.crosspoint/epub_123";
  const std::string path = "/Books/test.epub";
  ReaderStats stats;
  stats.begin(cache, path, true, 300);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 65000;
  BookReadingStats book;
  GlobalReadingStats global;
  bool saved = false;
  ASSERT_TRUE(stats.editBookDate(true, {2022, 3, 4}, book, global, saved));
  ASSERT_TRUE(saved);
  EXPECT_EQ(book.startDate.year, 2022);
  EXPECT_TRUE(book.startDateManual);
  EXPECT_EQ(book.totalReadingSeconds, 65U);
  EXPECT_EQ(global.totalReadingSeconds, 65U);
  ASSERT_TRUE(stats.editBookDate(false, {2023, 5, 6}, book, global, saved));
  ASSERT_TRUE(saved);
  EXPECT_FALSE(book.isCompleted);
  EXPECT_TRUE(book.finishedDateManual);
  stats.onRender(ReadingStatsFrame::Page);
  testMillis = 130000;
  ASSERT_TRUE(stats.finish());
  bool enabled = false;
  const auto persisted = readingstats::loadBook(cache, path, enabled);
  EXPECT_EQ(persisted.startDate.year, 2022);
  EXPECT_EQ(persisted.finishedDate.year, 2023);
  EXPECT_EQ(persisted.totalReadingSeconds, 130U);
  EXPECT_EQ(persisted.sessionCount, 1U);
  EXPECT_EQ(GlobalReadingStats::load().totalReadingSeconds, 130U);
}

TEST(ReadingStatsDateEdit, FailedSaveRetriesWithoutLosingDateOrDuplicatingTime) {
  for (const bool failBook : {true, false}) {
    Storage.reset();
    testMillis = 0;
    const std::string cache = "/.crosspoint/epub_123";
    const std::string path = "/Books/test.epub";
    ReaderStats stats;
    stats.begin(cache, path, true, 300);
    stats.onRender(ReadingStatsFrame::Page);
    testMillis = 65000;
    Storage.failNextRenameFrom(failBook ? readingstats::bookDirectory(cache) + "/stats_v5.bin.tmp"
                                        : "/.crosspoint/global_stats.bin.tmp");
    BookReadingStats book;
    GlobalReadingStats global;
    bool saved = true;
    ASSERT_TRUE(stats.editBookDate(true, {2021, 6, 7}, book, global, saved));
    EXPECT_FALSE(saved);
    ASSERT_TRUE(stats.finish());
    bool enabled = false;
    const auto persisted = readingstats::loadBook(cache, path, enabled);
    EXPECT_EQ(persisted.startDate.year, 2021);
    EXPECT_EQ(persisted.totalReadingSeconds, 65U);
    EXPECT_EQ(GlobalReadingStats::load().totalReadingSeconds, 65U);
  }
}

TEST(ReadingStatsDateEdit, InactiveSessionCannotWriteAndClearStaysManual) {
  Storage.reset();
  testMillis = 0;
  ReaderStats inactive;
  inactive.begin("/.crosspoint/epub_123", "/Books/test.epub", false, 300);
  BookReadingStats book;
  GlobalReadingStats global;
  bool saved = true;
  EXPECT_FALSE(inactive.editBookDate(true, {2020, 1, 1}, book, global, saved));
  ReaderStats active;
  active.begin("/.crosspoint/epub_123", "/Books/test.epub", true, 300);
  ASSERT_TRUE(active.editBookDate(true, {}, book, global, saved));
  EXPECT_TRUE(saved);
  EXPECT_TRUE(book.startDateManual);
  EXPECT_FALSE(book.startDate.isValid());
  EXPECT_FALSE(active.editBookDate(true, {2020, 2, 30}, book, global, saved));
}
