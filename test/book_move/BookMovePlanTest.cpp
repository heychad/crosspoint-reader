#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "util/BookMovePaths.h"
#include "util/BookMovePlan.h"

namespace {
using namespace bookmove;
Identities sample() {
  Identities value{};
  value[0] = {EntryKind::File, 0, 123456789, 1234};
  value[3] = {EntryKind::File, 0, 0, 14695981039346656037ULL};
  value[11] = {EntryKind::LegacyBookmark, 0, 82, 5678};
  value[21] = {EntryKind::Directory, 231, 0, 0};
  return value;
}

TEST(BookMovePlan, PreservesPathsAndAllIdentities) {
  auto plan = std::make_unique<PlanBytes>();
  const auto before = sample();
  size_t size = 0;
  ASSERT_TRUE(
      encodePlan("/Authors/Le Guin/Écrits.epub", "/Science Fiction/Écrits.epub", 0x123456789aULL, before, *plan, size));
  std::string_view from, to;
  uint64_t id = 0;
  Identities after{};
  ASSERT_TRUE(decodePlan(plan->data(), size, from, to, id, &after));
  EXPECT_EQ(from, "/Authors/Le Guin/Écrits.epub");
  EXPECT_EQ(to, "/Science Fiction/Écrits.epub");
  EXPECT_EQ(id, 0x123456789aULL);
  for (size_t i = 0; i < ENTRY_COUNT; ++i) {
    EXPECT_EQ(before[i].kind, after[i].kind);
    EXPECT_EQ(before[i].firstCluster, after[i].firstCluster);
    EXPECT_EQ(before[i].size, after[i].size);
    EXPECT_EQ(before[i].checksum, after[i].checksum);
  }
}

TEST(BookMovePlan, EveryTruncationAndSingleByteCorruptionFailsWithoutChangingOutputs) {
  auto plan = std::make_unique<PlanBytes>();
  size_t size = 0;
  ASSERT_TRUE(encodePlan("/Books/One.epub", "/Books/Two.epub", 7, sample(), *plan, size));
  std::string_view from = "before", to = "after";
  uint64_t id = 99;
  Identities entries = sample();
  for (size_t length = 0; length < size; ++length) {
    ASSERT_FALSE(decodePlan(plan->data(), length, from, to, id, &entries));
    EXPECT_EQ(from, "before");
    EXPECT_EQ(to, "after");
    EXPECT_EQ(id, 99);
    EXPECT_EQ(entries[21].firstCluster, 231u);
  }
  for (size_t i = 0; i < size; ++i) {
    (*plan)[i] ^= 1;
    ASSERT_FALSE(decodePlan(plan->data(), size, from, to, id, &entries));
    (*plan)[i] ^= 1;
  }
}

TEST(BookMovePlan, RejectsAmbiguousPathsAndInvalidIdentityKinds) {
  for (const char* path : {"book.epub", "/Books/../book.epub", "/Books//book.epub", "/.crosspoint/state.json",
                           "/Books/BOOK~1.EPU", "/Books/space .epub/", "/Books/back\\slash.epub"})
    EXPECT_FALSE(validMovePath(path)) << path;
  auto plan = std::make_unique<PlanBytes>();
  size_t size = 100;
  auto identities = sample();
  identities[0].kind = EntryKind::Missing;
  EXPECT_FALSE(encodePlan("/One.epub", "/Two.epub", 7, identities, *plan, size));
  EXPECT_EQ(size, 0u);
  identities = sample();
  identities[21].firstCluster = 0;
  EXPECT_FALSE(encodePlan("/One.epub", "/Two.epub", 7, identities, *plan, size));
  identities = sample();
  identities[2].checksum = 1;
  EXPECT_FALSE(encodePlan("/One.epub", "/Two.epub", 7, identities, *plan, size));
}

TEST(BookMovePlan, HandlesMaximumPathSizeAndAllEmptyOrMissingSidecars) {
  std::string from = "/Books/";
  while (from.size() + 105 < MAX_PATH_BYTES) from += std::string(100, 'x') + "/";
  from += std::string(MAX_PATH_BYTES - from.size() - 5, 'y') + ".epub";
  ASSERT_EQ(from.size(), MAX_PATH_BYTES);
  auto plan = std::make_unique<PlanBytes>();
  Identities identities{};
  identities[0] = {EntryKind::File, 0, 0, 14695981039346656037ULL};
  size_t size = 0;
  ASSERT_TRUE(encodePlan(from, "/Destination/Book.epub", 1, identities, *plan, size));
  std::string_view source, target;
  uint64_t id = 0;
  ASSERT_TRUE(decodePlan(plan->data(), size, source, target, id));
  EXPECT_EQ(source, from);
  EXPECT_FALSE(encodePlan(from + "x", target, 1, identities, *plan, size));
}

TEST(BookMovePlan, RejectsStructurallyInvalidKindsEvenWithMatchingChecksum) {
  auto plan = std::make_unique<PlanBytes>();
  size_t size = 0;
  ASSERT_TRUE(encodePlan("/One.epub", "/Two.epub", 7, sample(), *plan, size));
  (*plan)[HEADER_BYTES + FIRST_DIRECTORY * ENTRY_BYTES] = static_cast<uint8_t>(EntryKind::File);
  const size_t offset = HEADER_BYTES + FIRST_DIRECTORY * ENTRY_BYTES;
  for (size_t i = 1; i < 5; ++i) (*plan)[offset + i] = 0;
  put64(plan->data() + size - CHECKSUM_BYTES,
        readingstats::completionjournal::checksum(plan->data(), size - CHECKSUM_BYTES));
  std::string_view from = "unchanged", to = "unchanged";
  uint64_t id = 99;
  EXPECT_FALSE(decodePlan(plan->data(), size, from, to, id));
  EXPECT_EQ(from, "unchanged");
  EXPECT_EQ(id, 99u);
}

TEST(BookMovePaths, PreservesWholeCacheStatisticsAndBookBackupRoots) {
  const std::string from = "/Authors/Writer/Book.epub", to = "/Genres/Fiction/Book.epub";
  std::string source, target;
  for (size_t i = 21; i < ENTRY_COUNT; ++i) {
    ASSERT_TRUE(itemPaths(from, to, BookKind::Reflowable, i, source, target));
    EXPECT_EQ(ENTRIES[i].kind, EntryKind::Directory);
    EXPECT_NE(source, target);
    EXPECT_EQ(source, rootPath(from, BookKind::Reflowable, ENTRIES[i].root));
    EXPECT_EQ(target, rootPath(to, BookKind::Reflowable, ENTRIES[i].root));
  }
  ASSERT_TRUE(itemPaths(from, to, BookKind::Reflowable, 21, source, target));
  EXPECT_EQ(source, "/.crosspoint/" + cacheLeaf(from, BookKind::Reflowable));
  ASSERT_TRUE(itemPaths(from, to, BookKind::Reflowable, 25, source, target));
  EXPECT_EQ(target, "/.crossink-stats-backup/books/" + cacheLeaf(to, BookKind::Reflowable));
}

TEST(BookMovePaths, IncludesProtectedSidecarsAndBothLegacyBookmarkKeysAndArtifacts) {
  const std::string from = "/A/One.epub", to = "/B/Two.epub";
  std::string source, target;
  for (size_t i = 0; i < ENTRY_COUNT; ++i) {
    ASSERT_TRUE(itemPaths(from, to, BookKind::Reflowable, i, source, target));
    EXPECT_FALSE(source.empty());
    EXPECT_FALSE(target.empty());
    EXPECT_NE(source, target);
  }
  ASSERT_TRUE(itemPaths(from, to, BookKind::Reflowable, 1, source, target));
  EXPECT_EQ(source, from + ".key");
  ASSERT_TRUE(itemPaths(from, to, BookKind::Reflowable, 2, source, target));
  EXPECT_EQ(target, to + ".rights");
  ASSERT_TRUE(itemPaths(from, to, BookKind::Reflowable, 8, source, target));
  EXPECT_EQ(source, BookmarkUtil::getBookmarkPath(from));
  ASSERT_TRUE(itemPaths(from, to, BookKind::Reflowable, 11, source, target));
  EXPECT_EQ(source, "/.crosspoint/bookmarks/epub_" + std::to_string(credential_integrity::crc32(from)) + ".bin");
  ASSERT_TRUE(itemPaths(from, to, BookKind::Reflowable, 16, source, target));
  EXPECT_EQ(source, "/.crosspoint/bookmarks/" + cacheLeaf(from, BookKind::Reflowable) + ".bin");
  ASSERT_TRUE(itemPaths(from, to, BookKind::Reflowable, 20, source, target));
  EXPECT_TRUE(source.ends_with(".bookmark-edit.guard"));
}

TEST(BookMovePaths, XtcUsesItsOwnCacheAndStatisticsKeys) {
  std::string source, target;
  ASSERT_TRUE(itemPaths("/A/Book.xtc", "/B/Book.xtc", BookKind::Xtc, 21, source, target));
  EXPECT_TRUE(source.starts_with("/.crosspoint/xtc_"));
  ASSERT_TRUE(itemPaths("/A/Book.xtc", "/B/Book.xtc", BookKind::Xtc, 23, source, target));
  EXPECT_TRUE(target.starts_with("/.crosspoint/reading_stats/xtc_"));
  EXPECT_FALSE(itemPaths("/A.epub", "/B.epub", BookKind::Reflowable, ENTRY_COUNT, source, target));
}
}  // namespace
