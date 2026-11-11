#include <BookReadingStats.h>
#include <CompletionJournal.h>
#include <GlobalReadingStats.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace cj = readingstats::completionjournal;
static_assert(cj::BOOK_BYTES == BookReadingStats::CURRENT_FILE_SIZE);
static_assert(cj::GLOBAL_BYTES == GlobalReadingStats::CURRENT_FILE_SIZE);

TEST(CompletionJournal, RoundTripPreservesBothSnapshots) {
  cj::Book book{};
  cj::Global global{};
  for (size_t i = 0; i < book.size(); ++i) book[i] = static_cast<uint8_t>(i);
  for (size_t i = 0; i < global.size(); ++i) global[i] = static_cast<uint8_t>(255 - i);
  cj::Record record{};
  size_t size = 0;
  const std::string path = "/.crosspoint/reading_stats/epub_123456";
  ASSERT_TRUE(cj::encode(path, book, global, {}, record, size));
  std::string_view decodedPath;
  const uint8_t *decodedBook = nullptr, *decodedGlobal = nullptr;
  ASSERT_TRUE(cj::decode(record.data(), size, decodedPath, decodedBook, decodedGlobal));
  EXPECT_EQ(decodedPath, path);
  EXPECT_TRUE(std::equal(book.begin(), book.end(), decodedBook));
  EXPECT_TRUE(std::equal(global.begin(), global.end(), decodedGlobal));
}

TEST(CompletionJournal, PolicyIsBoundToRecordAndRejectsUnknownFlags) {
  cj::Record record{};
  size_t size = 0;
  ASSERT_TRUE(
      cj::encode("/.crosspoint/reading_stats/epub_1", {}, {}, {}, record, size, cj::REMOVE_RECENTS | cj::MOVE_TO_READ));
  EXPECT_EQ(record[9], cj::REMOVE_RECENTS | cj::MOVE_TO_READ);
  std::string_view path;
  const uint8_t *book = nullptr, *global = nullptr;
  ASSERT_TRUE(cj::decode(record.data(), size, path, book, global));
  record[9] ^= cj::MOVE_TO_READ;
  EXPECT_FALSE(cj::decode(record.data(), size, path, book, global));
  EXPECT_FALSE(cj::encode("/.crosspoint/reading_stats/epub_1", {}, {}, {}, record, size, 0x80));
}

TEST(CompletionJournal, DeferredSourceIsBoundAndPreviousVersionStillDecodes) {
  cj::Record record{};
  cj::Baseline baseline{};
  baseline.bookSize = 12;
  baseline.globalSize = 8;
  baseline.book[0] = 42;
  baseline.global[0] = 23;
  size_t size = 0;
  const std::string source = "/Authors/Le Guin/読書.epub";
  ASSERT_TRUE(cj::encode("/.crosspoint/reading_stats/epub_1", {}, {}, baseline, record, size,
                         cj::REMOVE_RECENTS | cj::DEFER_MOVE_TO_READ, source));
  EXPECT_EQ(record[4], 4);
  std::string_view decodedSource = "unchanged";
  ASSERT_TRUE(cj::decodeDeferredSource(record.data(), size, decodedSource));
  EXPECT_EQ(decodedSource, source);
  const uint8_t *oldBook = nullptr, *oldGlobal = nullptr;
  size_t oldBookSize = 0, oldGlobalSize = 0;
  ASSERT_TRUE(cj::decodeBaseline(record.data(), size, oldBook, oldBookSize, oldGlobal, oldGlobalSize));
  EXPECT_EQ(oldBookSize, 12u);
  EXPECT_EQ(oldGlobalSize, 8u);
  EXPECT_EQ(oldBook[0], 42);
  EXPECT_EQ(oldGlobal[0], 23);
  cj::Seal seal{};
  ASSERT_TRUE(cj::encodeSeal(5, record, size, seal));
  for (size_t i = 0; i < size; ++i) {
    auto damaged = record;
    damaged[i] ^= 1;
    EXPECT_FALSE(cj::matchesSeal(5, damaged, size, seal)) << i;
    EXPECT_FALSE(cj::decodeDeferredSource(record.data(), i, decodedSource)) << i;
  }
  ASSERT_TRUE(cj::encode("/.crosspoint/reading_stats/epub_1", {}, {}, {}, record, size));
  EXPECT_EQ(record[4], 3);
  decodedSource = "unchanged";
  EXPECT_FALSE(cj::decodeDeferredSource(record.data(), size, decodedSource));
  EXPECT_EQ(decodedSource, "unchanged");
}

TEST(CompletionJournal, DeferredSourceRequiresAValidBoundedPathAndExclusivePolicy) {
  cj::Record record{};
  size_t size = 0;
  for (const auto path : {"", "book.epub", "/a/../book.epub", "/a//book.epub", "/a/\nb.epub"}) {
    EXPECT_FALSE(
        cj::encode("/.crosspoint/reading_stats/epub_1", {}, {}, {}, record, size, cj::DEFER_MOVE_TO_READ, path))
        << path;
  }
  EXPECT_FALSE(cj::encode("/.crosspoint/reading_stats/epub_1", {}, {}, {}, record, size, 0, "/book.epub"));
  EXPECT_FALSE(cj::encode("/.crosspoint/reading_stats/epub_1", {}, {}, {}, record, size,
                          cj::DEFER_MOVE_TO_READ | cj::MOVE_TO_READ, "/book.epub"));
  const std::string maxPath = "/" + std::string(cj::MAX_SOURCE_PATH - 1, 'x');
  ASSERT_TRUE(
      cj::encode("/.crosspoint/reading_stats/epub_1", {}, {}, {}, record, size, cj::DEFER_MOVE_TO_READ, maxPath));
  std::string_view decoded;
  ASSERT_TRUE(cj::decodeDeferredSource(record.data(), size, decoded));
  EXPECT_EQ(decoded, maxPath);
  EXPECT_FALSE(
      cj::encode("/.crosspoint/reading_stats/epub_1", {}, {}, {}, record, size, cj::DEFER_MOVE_TO_READ, maxPath + "x"));
}

TEST(CompletionJournal, DeferredMoveBindsSourceAndOriginalTransaction) {
  cj::Record record{};
  size_t size = 0;
  constexpr uint64_t origin = 0x0123456789abcdefULL;
  const auto flags = cj::MOVE_TO_READ | cj::MOVE_FROM_DEFERRED | cj::REMOVE_RECENTS;
  ASSERT_TRUE(cj::encode("/.crosspoint/reading_stats/epub_1", {}, {}, {}, record, size, flags,
                         "/Authors/Writer/Book.epub", origin));
  EXPECT_EQ(record[4], 5);
  std::string_view source = "unchanged";
  uint64_t recovered = 0;
  ASSERT_TRUE(cj::decodeDeferredMove(record.data(), size, source, recovered));
  EXPECT_EQ(source, "/Authors/Writer/Book.epub");
  EXPECT_EQ(recovered, origin);
  for (size_t i = 0; i < size; ++i) {
    auto damaged = record;
    damaged[i] ^= 1;
    EXPECT_FALSE(cj::decodeDeferredMove(damaged.data(), size, source, recovered)) << i;
    EXPECT_FALSE(cj::decodeDeferredMove(record.data(), i, source, recovered)) << i;
  }
  EXPECT_FALSE(cj::encode("/.crosspoint/reading_stats/epub_1", {}, {}, {}, record, size, cj::MOVE_FROM_DEFERRED,
                          "/book.epub", origin));
  EXPECT_FALSE(cj::encode("/.crosspoint/reading_stats/epub_1", {}, {}, {}, record, size,
                          cj::MOVE_TO_READ | cj::MOVE_FROM_DEFERRED | cj::DEFER_MOVE_TO_READ, "/book.epub", origin));
  EXPECT_FALSE(cj::encode("/.crosspoint/reading_stats/epub_1", {}, {}, {}, record, size, cj::MOVE_TO_READ, "/book.epub",
                          origin));
}

TEST(CompletionJournal, RejectsTruncationAndSingleByteDamage) {
  cj::Record record{};
  size_t size = 0;
  ASSERT_TRUE(cj::encode("/.crosspoint/reading_stats/epub_1", {}, {}, {}, record, size));
  std::string_view path;
  const uint8_t *book = nullptr, *global = nullptr;
  for (size_t i = 0; i < size; ++i) EXPECT_FALSE(cj::decode(record.data(), i, path, book, global)) << i;
  for (size_t i = 0; i < size; ++i) {
    auto damaged = record;
    damaged[i] ^= 0x80;
    EXPECT_FALSE(cj::decode(damaged.data(), size, path, book, global)) << i;
  }
  EXPECT_FALSE(cj::decode(record.data(), size + 1, path, book, global));
  EXPECT_FALSE(cj::decode(nullptr, size, path, book, global));
}

TEST(CompletionJournal, RejectsPathsOutsideOwnedStatisticsDirectory) {
  cj::Record record{};
  size_t size = 123;
  for (const auto path : {"", "/.crosspoint/reading_stats/", "/.crosspoint/reading_stats/../global_stats.bin",
                          "/.crosspoint/reading_stats/epub_1/other", "/.crosspoint/reading_stats/epub 1"}) {
    EXPECT_FALSE(cj::encode(path, {}, {}, {}, record, size)) << path;
    EXPECT_EQ(size, 0u);
  }
  const std::string prefix = "/.crosspoint/reading_stats/";
  EXPECT_TRUE(cj::encode(prefix + std::string(cj::MAX_PATH - prefix.size(), 'a'), {}, {}, {}, record, size));
  EXPECT_FALSE(cj::encode(prefix + std::string(cj::MAX_PATH + 1 - prefix.size(), 'a'), {}, {}, {}, record, size));
}

TEST(CompletionJournal, SealBindsSlotAndCompleteRecord) {
  cj::Record first{}, second{};
  cj::Book book{};
  cj::Global global{};
  size_t firstSize = 0, secondSize = 0;
  ASSERT_TRUE(cj::encode("/.crosspoint/reading_stats/epub_1", book, global, {}, first, firstSize));
  book[9] = 1;
  ASSERT_TRUE(cj::encode("/.crosspoint/reading_stats/epub_1", book, global, {}, second, secondSize));
  cj::Seal seal{};
  ASSERT_TRUE(cj::encodeSeal(3, first, firstSize, seal));
  EXPECT_TRUE(cj::matchesSeal(3, first, firstSize, seal));
  EXPECT_FALSE(cj::matchesSeal(4, first, firstSize, seal));
  EXPECT_FALSE(cj::matchesSeal(3, second, secondSize, seal));
  EXPECT_FALSE(cj::matchesSeal(3, first, firstSize - 1, seal));
  for (size_t i = 0; i < seal.size(); ++i) {
    auto damaged = seal;
    damaged[i] ^= 1;
    EXPECT_FALSE(cj::matchesSeal(3, first, firstSize, damaged)) << i;
  }
  const auto preserved = seal;
  EXPECT_FALSE(cj::encodeSeal(64, first, firstSize, seal));
  EXPECT_EQ(seal, preserved);
}

TEST(CompletionJournal, CarriesExactPreviousSnapshotsAsReplayPreconditions) {
  cj::Book nextBook{};
  cj::Global nextGlobal{};
  cj::Baseline previous{};
  previous.bookSize = 69;
  previous.globalSize = 17;
  for (size_t i = 0; i < previous.bookSize; ++i) previous.book[i] = static_cast<uint8_t>(i + 1);
  for (size_t i = 0; i < previous.globalSize; ++i) previous.global[i] = static_cast<uint8_t>(i + 21);
  cj::Record record{};
  size_t size = 0;
  ASSERT_TRUE(cj::encode("/.crosspoint/reading_stats/epub_1", nextBook, nextGlobal, previous, record, size));
  const uint8_t *oldBook = nullptr, *oldGlobal = nullptr;
  size_t oldBookSize = 999, oldGlobalSize = 999;
  ASSERT_TRUE(cj::decodeBaseline(record.data(), size, oldBook, oldBookSize, oldGlobal, oldGlobalSize));
  ASSERT_EQ(oldBookSize, previous.bookSize);
  ASSERT_EQ(oldGlobalSize, previous.globalSize);
  EXPECT_TRUE(std::equal(previous.book.begin(), previous.book.begin() + previous.bookSize, oldBook));
  EXPECT_TRUE(std::equal(previous.global.begin(), previous.global.begin() + previous.globalSize, oldGlobal));
  cj::Seal seal{};
  ASSERT_TRUE(cj::encodeSeal(0, record, size, seal));
  previous.book[0] ^= 1;
  cj::Record changed{};
  size_t changedSize = 0;
  ASSERT_TRUE(cj::encode("/.crosspoint/reading_stats/epub_1", nextBook, nextGlobal, previous, changed, changedSize));
  EXPECT_FALSE(cj::matchesSeal(0, changed, changedSize, seal));
  previous.bookSize = 74;
  EXPECT_FALSE(cj::encode("/.crosspoint/reading_stats/epub_1", nextBook, nextGlobal, previous, changed, changedSize));
}

TEST(CompletionJournal, RetirementReceiptIsBoundAndDistinctFromSeal) {
  cj::Record record{};
  size_t size = 0;
  ASSERT_TRUE(cj::encode("/.crosspoint/reading_stats/epub_1", {}, {}, {}, record, size));
  cj::Seal seal{}, retirement{};
  ASSERT_TRUE(cj::encodeSeal(7, record, size, seal));
  ASSERT_TRUE(cj::encodeRetirement(7, record, size, retirement));
  EXPECT_TRUE(cj::matchesRetirement(7, record, size, retirement));
  EXPECT_FALSE(cj::matchesRetirement(7, record, size, seal));
  EXPECT_FALSE(cj::matchesSeal(7, record, size, retirement));
  EXPECT_FALSE(cj::matchesRetirement(8, record, size, retirement));
}
