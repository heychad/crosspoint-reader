#include <BookCompletion.h>
#include <HalStorage.h>
#include <ReadingStatsStore.h>
#include <ZipFile.h>
#include <gtest/gtest.h>
#include <cstring>

namespace {
class ReadingStatsStoreTest : public testing::Test {
  void SetUp() override { Storage.reset(); }
};
constexpr char CACHE[] = "/.crosspoint/epub_123";
constexpr char BOOK[] = "/Authors/Author/Book.epub";
}  // namespace

TEST_F(ReadingStatsStoreTest, ImportsCrossInkStatsWithoutChangingLegacyFiles) {
  const std::string legacy = "/.crosspoint/epub_" + std::to_string(ZipFile::fnvHash64(BOOK, sizeof(BOOK) - 1));
  BookReadingStats original;
  original.totalReadingSeconds = 900;
  original.isCompleted = true;
  ASSERT_TRUE(original.save(legacy));
  bool enabled = false;
  const auto imported = readingstats::loadBook(CACHE, BOOK, enabled);
  EXPECT_TRUE(enabled);
  EXPECT_EQ(imported.totalReadingSeconds, 900U);
  EXPECT_TRUE(imported.isCompleted);
  const auto destination = readingstats::bookDirectory(CACHE);
  EXPECT_EQ(destination, "/.crosspoint/reading_stats/epub_123");
  ASSERT_TRUE(readingstats::saveBook(destination, imported));
  EXPECT_EQ(BookReadingStats::load(legacy).totalReadingSeconds, 900U);
  EXPECT_TRUE(Storage.exists((legacy + "/stats_v5.bin").c_str()));
}

TEST_F(ReadingStatsStoreTest, ClosedBookDateEditReloadsAndPreservesCrossInkHistory) {
  HalFile book;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", BOOK, book));
  ASSERT_TRUE(book.close());
  const std::string legacy = "/.crosspoint/epub_" + std::to_string(ZipFile::fnvHash64(BOOK, sizeof(BOOK) - 1));
  BookReadingStats original;
  original.totalReadingSeconds = 900;
  original.isCompleted = true;
  ASSERT_TRUE(original.save(legacy));
  BookReadingStats updated;
  ASSERT_TRUE(readingstats::editClosedBookDate(CACHE, BOOK, true, {2026, 11, 6}, updated));
  EXPECT_EQ(updated.totalReadingSeconds, 900U);
  EXPECT_TRUE(updated.isCompleted);
  EXPECT_TRUE(updated.startDateManual);
  EXPECT_EQ(updated.startDate.year, 2026);
  EXPECT_EQ(BookReadingStats::load(legacy).totalReadingSeconds, 900U);
  EXPECT_FALSE(BookReadingStats::load(legacy).startDate.isValid());
  bool enabled = false;
  const auto saved = readingstats::loadBook(CACHE, BOOK, enabled);
  EXPECT_EQ(saved.startDate.year, 2026);
  EXPECT_EQ(saved.totalReadingSeconds, 900U);
  EXPECT_TRUE(readingstats::editClosedBookDate(CACHE, BOOK, true, {}, updated));
  EXPECT_FALSE(updated.startDate.isValid());
  EXPECT_TRUE(updated.startDateManual);
}

TEST_F(ReadingStatsStoreTest, ClosedBookDateEditRefusesMissingUnreadableOrInvalidData) {
  BookReadingStats updated;
  EXPECT_FALSE(readingstats::editClosedBookDate(CACHE, BOOK, true, {2026, 11, 6}, updated));
  HalFile book;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", BOOK, book));
  ASSERT_TRUE(book.close());
  EXPECT_FALSE(readingstats::editClosedBookDate(CACHE, BOOK, true, {2026, 11, 6}, updated));
  const auto directory = readingstats::bookDirectory(CACHE);
  BookReadingStats original;
  original.totalReadingSeconds = 900;
  ASSERT_TRUE(readingstats::saveBook(directory, original));
  EXPECT_FALSE(readingstats::editClosedBookDate(CACHE, BOOK, true, {2026, 2, 30}, updated));
  Storage.failReadsFor(directory + "/stats_v5.bin");
  EXPECT_FALSE(readingstats::editClosedBookDate(CACHE, BOOK, true, {2026, 11, 6}, updated));
  EXPECT_EQ(updated.totalReadingSeconds, 0U);
}

TEST_F(ReadingStatsStoreTest, ClosedBookDateEditMigratesVersionFourWithoutChangingSource) {
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", BOOK, file));
  ASSERT_TRUE(file.close());
  const std::string legacy = "/.crosspoint/epub_" + std::to_string(ZipFile::fnvHash64(BOOK, sizeof(BOOK) - 1));
  const std::string oldPath = legacy + "/stats_v4.bin";
  ASSERT_TRUE(Storage.openFileForWrite("TEST", oldPath, file));
  uint8_t bytes[69]{};
  bytes[0] = 4;
  bytes[3] = 0x84;
  bytes[4] = 0x03;  // 900 seconds, little endian.
  bytes[11] = 1;
  ASSERT_EQ(file.write(bytes, sizeof(bytes)), sizeof(bytes));
  ASSERT_TRUE(file.close());
  BookReadingStats updated;
  ASSERT_TRUE(readingstats::editClosedBookDate(CACHE, BOOK, false, {2026, 11, 6}, updated));
  EXPECT_EQ(updated.totalReadingSeconds, 900U);
  EXPECT_TRUE(updated.isCompleted);
  EXPECT_EQ(updated.finishedDate.year, 2026);
  ASSERT_TRUE(Storage.openFileForRead("TEST", oldPath, file));
  uint8_t after[sizeof(bytes)]{};
  ASSERT_EQ(file.read(after, sizeof(after)), static_cast<int>(sizeof(after)));
  EXPECT_EQ(std::memcmp(bytes, after, sizeof(bytes)), 0);
  EXPECT_TRUE(file.close());
}

TEST_F(ReadingStatsStoreTest, ClosedBookDateEditKeepsRecoveryFilesAfterFailedSave) {
  HalFile book;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", BOOK, book));
  ASSERT_TRUE(book.close());
  const auto directory = readingstats::bookDirectory(CACHE);
  BookReadingStats original;
  original.totalReadingSeconds = 900;
  ASSERT_TRUE(readingstats::saveBook(directory, original));
  Storage.failNextRenameFrom(directory + "/stats_v5.bin.tmp");
  BookReadingStats updated;
  EXPECT_FALSE(readingstats::editClosedBookDate(CACHE, BOOK, false, {2026, 11, 6}, updated));
  EXPECT_TRUE(Storage.exists((directory + "/stats_v5.bin.tmp").c_str()));
  EXPECT_EQ(updated.totalReadingSeconds, 0U);
  EXPECT_FALSE(readingstats::editClosedBookDate(CACHE, BOOK, false, {2026, 11, 6}, updated));
  bool enabled = false, found = false;
  const auto reopened = readingstats::loadBook(CACHE, BOOK, enabled, &found, BookReadingStats::LoadMode::ReadOnly);
  EXPECT_TRUE(found);
  EXPECT_FALSE(reopened.writable);
  EXPECT_TRUE(Storage.exists((directory + "/stats_v5.bin.tmp").c_str()));
}

TEST_F(ReadingStatsStoreTest, CanonicalStatsWinAndBookDisableMarkerIsHonored) {
  BookReadingStats original;
  original.totalReadingSeconds = 900;
  ASSERT_TRUE(original.save(CACHE));
  const auto destination = readingstats::bookDirectory(CACHE);
  original.totalReadingSeconds = 1200;
  ASSERT_TRUE(original.save(destination));
  HalFile f;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", std::string(CACHE) + "/reading_stats_off", f));
  f.close();
  bool enabled = true;
  auto loaded = readingstats::loadBook(CACHE, BOOK, enabled);
  EXPECT_FALSE(enabled);
  EXPECT_EQ(loaded.totalReadingSeconds, 1200U);
}

TEST_F(ReadingStatsStoreTest, ReadOnlyBookLookupDoesNotPromoteRecoveryFile) {
  const auto directory = readingstats::bookDirectory(CACHE);
  const auto primary = directory + "/stats_v5.bin";
  BookReadingStats original;
  original.totalReadingSeconds = 900;
  ASSERT_TRUE(original.save(directory));
  ASSERT_TRUE(Storage.rename(primary.c_str(), (primary + ".tmp").c_str()));
  bool enabled = false;
  bool found = false;
  const auto stats = readingstats::loadBook(CACHE, BOOK, enabled, &found, BookReadingStats::LoadMode::ReadOnly);
  EXPECT_TRUE(enabled);
  EXPECT_TRUE(found);
  EXPECT_EQ(stats.totalReadingSeconds, 900U);
  EXPECT_FALSE(Storage.exists(primary.c_str()));
  EXPECT_TRUE(Storage.exists((primary + ".tmp").c_str()));
}

TEST_F(ReadingStatsStoreTest, ClearCacheKeepsUnopenedCrossInkHistoryAndDisableMarker) {
  const std::string legacy = "/.crosspoint/epub_" + std::to_string(ZipFile::fnvHash64(BOOK, sizeof(BOOK) - 1));
  BookReadingStats original;
  original.totalReadingSeconds = 900;
  ASSERT_TRUE(original.save(legacy));
  HalFile marker;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", legacy + "/reading_stats_off", marker));
  marker.close();
  ASSERT_TRUE(readingstats::preserveCache(legacy));
  ASSERT_TRUE(Storage.removeDir(legacy.c_str()));
  bool enabled = true;
  const auto restored = readingstats::loadBook(CACHE, BOOK, enabled);
  EXPECT_FALSE(enabled);
  EXPECT_EQ(restored.totalReadingSeconds, 900U);
}

TEST_F(ReadingStatsStoreTest, UnsupportedStatsPreventDestructiveCacheClear) {
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", std::string(CACHE) + "/stats_v5.bin", file));
  file.write(uint8_t{6});
  file.close();
  EXPECT_FALSE(readingstats::preserveCache(CACHE));
  EXPECT_TRUE(Storage.exists((std::string(CACHE) + "/stats_v5.bin").c_str()));
}

TEST_F(ReadingStatsStoreTest, CompletionFilterKeepsMissingAndUnreadableBooksUnknown) {
  EXPECT_FALSE(readingstats::bookCompletion(BOOK).has_value());
  HalFile book;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", BOOK, book));
  book.close();
  const std::string cache = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(BOOK));
  ASSERT_EQ(readingstats::bookCompletion(BOOK), std::optional<bool>(false));
  BookReadingStats stats;
  stats.isCompleted = true;
  ASSERT_TRUE(stats.save(cache));
  ASSERT_EQ(readingstats::bookCompletion(BOOK), std::optional<bool>(true));
  Storage.failReadsFor(cache + "/stats_v5.bin");
  EXPECT_FALSE(readingstats::bookCompletion(BOOK).has_value());
}

TEST_F(ReadingStatsStoreTest, ClearCachePreservesReaderPreferencesAndRecoveryFiles) {
  for (const auto* name : {"/reader_settings.bin", "/reader_settings.bin.bak", "/reader_prefs.json",
                           "/reader_prefs.json.tmp", "/book.bin"}) {
    HalFile file;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", std::string(CACHE) + name, file));
    file.write(uint8_t{42});
    file.close();
  }
  ASSERT_TRUE(readingstats::clearCache(CACHE));
  for (const auto* name :
       {"/reader_settings.bin", "/reader_settings.bin.bak", "/reader_prefs.json", "/reader_prefs.json.tmp"}) {
    EXPECT_TRUE(Storage.exists((std::string(CACHE) + name).c_str()));
  }
  EXPECT_FALSE(Storage.exists((std::string(CACHE) + "/book.bin").c_str()));
}

TEST_F(ReadingStatsStoreTest, FinishedBookMoveRollsBackStatsOrCommitsThem) {
  BookReadingStats original;
  original.totalReadingSeconds = 1234;
  ASSERT_TRUE(original.save(CACHE));
  const auto from = readingstats::bookDirectory(CACHE);
  const auto to = readingstats::bookDirectory("/.crosspoint/epub_456");
  {
    readingstats::BookMove move(CACHE, "/.crosspoint/epub_456", BOOK);
    ASSERT_TRUE(move.prepare());
    EXPECT_TRUE(Storage.exists((to + "/stats_v5.bin").c_str()));
  }
  EXPECT_TRUE(Storage.exists((from + "/stats_v5.bin").c_str()));
  EXPECT_FALSE(Storage.exists((to + "/stats_v5.bin").c_str()));
  {
    readingstats::BookMove move(CACHE, "/.crosspoint/epub_456", BOOK);
    ASSERT_TRUE(move.prepare());
    move.commit();
  }
  EXPECT_EQ(BookReadingStats::load(to).totalReadingSeconds, 1234U);
}

TEST_F(ReadingStatsStoreTest, FinishedBookMoveNeverOverwritesAnotherHistory) {
  BookReadingStats first;
  first.totalReadingSeconds = 100;
  ASSERT_TRUE(first.save(CACHE));
  BookReadingStats second;
  second.totalReadingSeconds = 200;
  const auto to = readingstats::bookDirectory("/.crosspoint/epub_456");
  ASSERT_TRUE(second.save(to));
  readingstats::BookMove move(CACHE, "/.crosspoint/epub_456", BOOK);
  EXPECT_FALSE(move.prepare());
  EXPECT_EQ(BookReadingStats::load(to).totalReadingSeconds, 200U);
}

TEST_F(ReadingStatsStoreTest, MoveImportsLegacyHistoryAndDisabledFlagWithoutRecordedSession) {
  const std::string legacy = "/.crosspoint/epub_" + std::to_string(ZipFile::fnvHash64(BOOK, sizeof(BOOK) - 1));
  BookReadingStats original;
  original.totalReadingSeconds = 900;
  ASSERT_TRUE(original.save(legacy));
  HalFile marker;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", legacy + "/reading_stats_off", marker));
  marker.close();
  readingstats::BookMove move(CACHE, "/.crosspoint/epub_456", BOOK);
  ASSERT_TRUE(move.prepare());
  move.commit();
  bool enabled = true;
  const auto moved = readingstats::loadBook("/.crosspoint/epub_456", "/Read/Book.epub", enabled);
  EXPECT_EQ(moved.totalReadingSeconds, 900U);
  EXPECT_FALSE(enabled);
  EXPECT_TRUE(Storage.exists((legacy + "/stats_v5.bin").c_str()));
}

TEST_F(ReadingStatsStoreTest, OpaqueHistoryDoesNotBlockBookReplacementCacheInvalidation) {
  for (const auto& name : {"/stats_v5.bin", "/stats_v99.bin", "/book.bin", "/sections/0.bin", "/cover.bmp"}) {
    HalFile file;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", std::string(CACHE) + name, file));
    const uint8_t bytes[] = {99, 12, 34};
    file.write(bytes, sizeof(bytes));
    file.close();
  }
  Storage.failReadsFor(std::string(CACHE) + "/stats_v5.bin");
  ASSERT_TRUE(readingstats::clearCache(CACHE));
  EXPECT_TRUE(Storage.exists((std::string(CACHE) + "/stats_v5.bin").c_str()));
  EXPECT_TRUE(Storage.exists((std::string(CACHE) + "/stats_v99.bin").c_str()));
  EXPECT_FALSE(Storage.exists((std::string(CACHE) + "/book.bin").c_str()));
  EXPECT_FALSE(Storage.exists((std::string(CACHE) + "/sections/0.bin").c_str()));
  EXPECT_FALSE(Storage.exists((std::string(CACHE) + "/cover.bmp").c_str()));
}

TEST_F(ReadingStatsStoreTest, FailedStatsMigrationStillInvalidatesRebuildableData) {
  BookReadingStats original;
  original.totalReadingSeconds = 321;
  ASSERT_TRUE(original.save(CACHE));
  HalFile metadata;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", std::string(CACHE) + "/book.bin", metadata));
  metadata.write(uint8_t{123});
  metadata.close();
  Storage.failNextRenameFrom(readingstats::bookDirectory(CACHE) + "/stats_v5.bin.tmp");
  ASSERT_TRUE(readingstats::clearCache(CACHE));
  EXPECT_EQ(BookReadingStats::load(CACHE).totalReadingSeconds, 321U);
  EXPECT_FALSE(Storage.exists((std::string(CACHE) + "/book.bin").c_str()));
}
