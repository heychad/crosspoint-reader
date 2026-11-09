#include <BookTracking.h>
#include <HalStorage.h>
#include <ReaderStats.h>
#include <ReadingStatsStore.h>
#include <ZipFile.h>
#include <gtest/gtest.h>

namespace {
constexpr char CACHE[] = "/.crosspoint/epub_123";
constexpr char BOOK[] = "/Authors/Author/Book.epub";
class BookTrackingTest : public testing::Test {
  void SetUp() override { Storage.reset(); }
};
std::string legacyCache() { return "/.crosspoint/epub_" + std::to_string(ZipFile::fnvHash64(BOOK, sizeof(BOOK) - 1)); }
void marker(const std::string& directory) {
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", directory + "/reading_stats_off", file));
  ASSERT_TRUE(file.close());
}
}  // namespace

TEST_F(BookTrackingTest, DisableAndEnablePreserveStatisticsAndPosition) {
  const auto directory = readingstats::bookDirectory(CACHE);
  BookReadingStats stats;
  stats.totalReadingSeconds = 42;
  ASSERT_TRUE(readingstats::saveBook(directory, stats));
  HalFile position;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", std::string(CACHE) + "/progress.bin", position));
  ASSERT_EQ(position.write(uint8_t{7}), 1U);
  ASSERT_TRUE(position.close());
  EXPECT_EQ(booktracking::state(CACHE, BOOK), booktracking::State::Enabled);
  ASSERT_TRUE(booktracking::setEnabled(CACHE, BOOK, false));
  EXPECT_EQ(booktracking::state(CACHE, BOOK), booktracking::State::Disabled);
  bool enabled = true;
  EXPECT_EQ(readingstats::loadBook(CACHE, BOOK, enabled).totalReadingSeconds, 42U);
  EXPECT_FALSE(enabled);
  ASSERT_TRUE(booktracking::setEnabled(CACHE, BOOK, true));
  EXPECT_EQ(booktracking::state(CACHE, BOOK), booktracking::State::Enabled);
  EXPECT_TRUE(Storage.exists((std::string(CACHE) + "/progress.bin").c_str()));
  EXPECT_EQ(BookReadingStats::load(directory).totalReadingSeconds, 42U);
}

TEST_F(BookTrackingTest, LegacyMarkersAreHonoredAndRemovedTogether) {
  marker(CACHE);
  marker(legacyCache());
  marker(readingstats::bookDirectory(legacyCache()));
  ASSERT_EQ(booktracking::state(CACHE, BOOK), booktracking::State::Disabled);
  ASSERT_TRUE(booktracking::setEnabled(CACHE, BOOK, true));
  EXPECT_EQ(booktracking::state(CACHE, BOOK), booktracking::State::Enabled);
}

TEST_F(BookTrackingTest, UnknownMarkerPreventsAnyChanges) {
  const auto existing = std::string(CACHE) + "/reading_stats_off";
  marker(CACHE);
  Storage.failReadsFor((legacyCache() + "/reading_stats_off").c_str());
  EXPECT_EQ(booktracking::state(CACHE, BOOK), booktracking::State::Unknown);
  EXPECT_FALSE(booktracking::setEnabled(CACHE, BOOK, true));
  EXPECT_TRUE(Storage.exists(existing.c_str()));
}

TEST_F(BookTrackingTest, FailedRemovalRetainsRemainingMarkersAndCanBeRetried) {
  const auto legacy = legacyCache();
  marker(CACHE);
  marker(legacy);
  Storage.failNextRemove(legacy + "/reading_stats_off");
  EXPECT_FALSE(booktracking::setEnabled(CACHE, BOOK, true));
  EXPECT_EQ(booktracking::state(CACHE, BOOK), booktracking::State::Disabled);
  EXPECT_TRUE(Storage.exists((legacy + "/reading_stats_off").c_str()));
  EXPECT_TRUE(booktracking::setEnabled(CACHE, BOOK, true));
  EXPECT_EQ(booktracking::state(CACHE, BOOK), booktracking::State::Enabled);
}

TEST_F(BookTrackingTest, MarkerDirectoryIsNotRemoved) {
  const auto markerPath = std::string(CACHE) + "/reading_stats_off";
  ASSERT_TRUE(Storage.mkdir(markerPath.c_str()));
  EXPECT_EQ(booktracking::state(CACHE, BOOK), booktracking::State::Unknown);
  EXPECT_FALSE(booktracking::setEnabled(CACHE, BOOK, true));
  EXPECT_TRUE(Storage.exists(markerPath.c_str()));
}

TEST_F(BookTrackingTest, UnknownMarkerPreventsReaderFromStartingTracking) {
  const auto markerPath = readingstats::bookDirectory(CACHE) + "/reading_stats_off";
  Storage.failReadsFor(markerPath);
  ReaderStats reader;
  reader.begin(CACHE, BOOK, true, 300);
  BookReadingStats book;
  GlobalReadingStats global;
  bool saved = false;
  EXPECT_FALSE(reader.snapshot(book, global, saved));
  EXPECT_TRUE(reader.finish());
  EXPECT_FALSE(Storage.exists("/.crosspoint/global_stats.bin"));
}

TEST_F(BookTrackingTest, UnknownMarkerBlocksBookMoveBeforeAnyMigration) {
  const auto markerPath = readingstats::bookDirectory(CACHE) + "/reading_stats_off";
  Storage.failReadsFor(markerPath);
  readingstats::BookMove move(CACHE, "/.crosspoint/epub_456", BOOK);
  EXPECT_FALSE(move.prepare());
  EXPECT_FALSE(Storage.exists(readingstats::bookDirectory(CACHE).c_str()));
  EXPECT_FALSE(Storage.exists("/.crosspoint/reading_stats/epub_456"));
}

TEST_F(BookTrackingTest, UnknownStatisticsDoNotStartReaderOrFallBackToOlderHistory) {
  const auto canonical = readingstats::bookDirectory(CACHE);
  BookReadingStats existing;
  existing.totalReadingSeconds = 123;
  ASSERT_TRUE(readingstats::saveBook(canonical, existing));
  BookReadingStats older;
  older.totalReadingSeconds = 50;
  ASSERT_TRUE(readingstats::saveBook(CACHE, older));
  Storage.failReadsFor(canonical + "/stats_v5.bin");
  bool enabled = true;
  bool found = false;
  const auto result = readingstats::loadBook(CACHE, BOOK, enabled, &found);
  EXPECT_TRUE(found);
  EXPECT_FALSE(result.writable);
  ReaderStats reader;
  reader.begin(CACHE, BOOK, true, 300);
  BookReadingStats snapshot;
  GlobalReadingStats global;
  bool saved = false;
  EXPECT_FALSE(reader.snapshot(snapshot, global, saved));
  EXPECT_TRUE(reader.finish());
  EXPECT_EQ(BookReadingStats::load(CACHE).totalReadingSeconds, 50U);
}

TEST_F(BookTrackingTest, FailedStatsReadCloseIsNotWritable) {
  const auto canonical = readingstats::bookDirectory(CACHE);
  BookReadingStats existing;
  existing.totalReadingSeconds = 123;
  ASSERT_TRUE(readingstats::saveBook(canonical, existing));
  Storage.failNextCloseFor(canonical + "/stats_v5.bin");
  EXPECT_FALSE(BookReadingStats::load(canonical).writable);
}

TEST_F(BookTrackingTest, FailedRecoveryLookupDoesNotSelectOlderStats) {
  const auto canonical = readingstats::bookDirectory(CACHE);
  HalFile older;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", canonical + "/stats_v4.bin", older));
  ASSERT_EQ(older.write(uint8_t{4}), 1U);
  ASSERT_TRUE(older.close());
  Storage.failReadsFor(canonical + "/stats_v5.bin.tmp");
  EXPECT_FALSE(BookReadingStats::load(canonical).writable);
}
