#include <ClippingStore.h>
#include <CredentialIntegrity.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <functional>
#include <limits>

#include "util/BookmarkUtil.h"
#include "util/CrossInkBookmarks.h"
#include "util/SavedItems.h"

namespace {
const std::string BOOK = "/Authors/Book.epub";
template <typename T>
void pod(std::string& data, T value) {
  data.append(reinterpret_cast<const char*>(&value), sizeof(value));
}
void text(std::string& data, const std::string& value) {
  pod<uint32_t>(data, value.size());
  data += value;
}
std::string legacyPath(bool crc = true) {
  return "/.crosspoint/bookmarks/epub_" +
         (crc ? std::to_string(credential_integrity::crc32(BOOK)) : std::to_string(std::hash<std::string>{}(BOOK))) +
         ".bin";
}
std::string legacyData(uint8_t version, uint16_t count = 2, const std::string& book = BOOK) {
  std::string data;
  pod(data, version);
  if (version == 2)
    pod<uint8_t>(data, count);
  else
    pod(data, count);
  text(data, "Book");
  text(data, "Author");
  text(data, book);
  for (uint16_t i = 0; i < count; ++i) {
    pod<uint16_t>(data, i + 1);
    pod<float>(data, std::min(1.0f, 0.25f * (i + 1)));
    pod<uint32_t>(data, 1234);
    data += std::string("Chapter") + std::string(41, '\0');
    if (version >= 4) pod<uint16_t>(data, i + 3);
    if (version >= 5) data += std::string("Snippet") + std::string(57, '\0');
  }
  return data;
}
void addClipping() {
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook(BOOK, "Book", "Author", "epub"));
  ASSERT_EQ(store.addClipping(1, 2, 2, 6, 0, 2, 3, "Chapter", 0, "Text", 1, 0, 4), ClippingStore::AddResult::Added);
  store.unload();
}
}  // namespace

TEST(SavedItems, EmptyMetadataHasNoKinds) {
  fake::reset();
  uint8_t kinds = 99;
  EXPECT_TRUE(saveditems::inspect(BOOK, kinds));
  EXPECT_EQ(kinds, 0);
}

TEST(SavedItems, CountsNativeWithoutRetainingTextAndIgnoresOtherArrays) {
  fake::reset();
  fake::add(BookmarkUtil::getBookmarkPath(BOOK),
            "{\"other\":[{}],\"bookmarks\":[{\"summary\":\"" + std::string(8000, 'a') + "\"},{\"name\":\"Second\"}]}");
  uint8_t kinds = 0;
  EXPECT_TRUE(saveditems::inspect(BOOK, kinds));
  EXPECT_EQ(kinds, saveditems::BOOKMARKS);
  EXPECT_TRUE(fake::writesByPath.empty());
}

TEST(SavedItems, MalformedAndFailedNativeReadsReportIncomplete) {
  for (const auto& contents : {"{\"bookmarks\":[{}", "{", "{\"bookmarks\":[", "{\"bookmarks\":[{]}"}) {
    fake::reset();
    fake::add(BookmarkUtil::getBookmarkPath(BOOK), contents);
    uint8_t kinds = 0;
    EXPECT_FALSE(saveditems::inspect(BOOK, kinds)) << contents;
  }
  fake::reset();
  fake::add(BookmarkUtil::getBookmarkPath(BOOK), "{\"bookmarks\":[{}]}");
  fake::failRead = 0;
  uint8_t kinds = 0;
  EXPECT_FALSE(saveditems::inspect(BOOK, kinds));
}

TEST(SavedItems, NativeScannerAllocationFailureIsVisible) {
  fake::reset();
  fake::add(BookmarkUtil::getBookmarkPath(BOOK), "{\"bookmarks\":[{}]}");
  fake::failAlloc = 0;
  uint8_t kinds = 0;
  EXPECT_FALSE(saveditems::inspect(BOOK, kinds));
  EXPECT_EQ(kinds, 0);
}

TEST(SavedItems, AllThreeSourcesRemainIndependentAndUnchanged) {
  fake::reset();
  fake::add(BookmarkUtil::getBookmarkPath(BOOK), "{\"bookmarks\":[{}]}");
  const auto original = legacyData(5);
  fake::add(legacyPath(), original);
  addClipping();
  fake::writesByPath.clear();
  uint8_t kinds = 0;
  EXPECT_TRUE(saveditems::inspect(BOOK, kinds));
  EXPECT_EQ(kinds, saveditems::BOOKMARKS | saveditems::CROSSINK_BOOKMARKS | saveditems::HAS_CLIPPINGS);
  EXPECT_EQ(std::string(fake::files.at(legacyPath())->bytes.begin(), fake::files.at(legacyPath())->bytes.end()),
            original);
  EXPECT_TRUE(fake::writesByPath.empty());
}

TEST(SavedItems, LegacyVersionsPreservePositionsAndBothFilenameSchemes) {
  for (uint8_t version = 2; version <= 5; ++version) {
    for (bool crc : {true, false}) {
      fake::reset();
      fake::add(legacyPath(crc), legacyData(version));
      CrossInkBookmarks bookmarks;
      ASSERT_TRUE(bookmarks.open(BOOK));
      EXPECT_EQ(bookmarks.count(), 2);
      CrossInkBookmarks::Entry entry;
      ASSERT_TRUE(bookmarks.read(1, entry));
      EXPECT_EQ(entry.spine, 2);
      EXPECT_FLOAT_EQ(entry.progress, .5f);
      EXPECT_EQ(entry.paragraph, version >= 4 ? 4 : UINT16_MAX);
      EXPECT_STREQ(entry.chapter, "Chapter");
      EXPECT_STREQ(entry.snippet, version >= 5 ? "Snippet" : "");
      EXPECT_FALSE(bookmarks.read(2, entry));
    }
  }
}

TEST(SavedItems, LegacyHandlesEmptyAndFailedOpenWithoutClosingEmptyHandle) {
  fake::reset();
  CrossInkBookmarks absent;
  EXPECT_FALSE(absent.open(BOOK));
  absent.close();
  EXPECT_EQ(fake::emptyCloseCalls, 0U);

  fake::reset();
  fake::add(legacyPath(), legacyData(5));
  fake::failOpenPath = legacyPath();
  CrossInkBookmarks failed;
  EXPECT_FALSE(failed.open(BOOK));
  EXPECT_TRUE(failed.unreadable());
  failed.close();
  EXPECT_EQ(fake::emptyCloseCalls, 0U);
}

TEST(SavedItems, LegacyRejectsOversizedHeadersAndTruncationAndWrongPath) {
  for (const auto& data : {legacyData(1), legacyData(6), legacyData(5, 2, "/wrong.epub"),
                           legacyData(5).substr(0, legacyData(5).size() - 1), legacyData(5, 1025)}) {
    fake::reset();
    fake::add(legacyPath(), data);
    CrossInkBookmarks bookmarks;
    EXPECT_FALSE(bookmarks.open(BOOK));
    EXPECT_TRUE(bookmarks.unreadable());
    uint8_t kinds = 0;
    EXPECT_FALSE(saveditems::inspect(BOOK, kinds));
  }
  auto data = legacyData(5);
  const uint32_t excessive = 0xffffffffu;
  std::memcpy(data.data() + 3, &excessive, 4);
  fake::reset();
  fake::add(legacyPath(), data);
  CrossInkBookmarks bookmarks;
  EXPECT_FALSE(bookmarks.open(BOOK));
}

TEST(SavedItems, LegacyRejectsNonFiniteAndOutOfRangeProgress) {
  for (float progress : {-1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN()}) {
    fake::reset();
    auto data = legacyData(5);
    const size_t offset = 3 + 4 + 4 + 4 + 6 + 4 + BOOK.size() + 2;
    std::memcpy(data.data() + offset, &progress, 4);
    fake::add(legacyPath(), data);
    CrossInkBookmarks bookmarks;
    ASSERT_TRUE(bookmarks.open(BOOK));
    CrossInkBookmarks::Entry entry;
    EXPECT_FALSE(bookmarks.read(0, entry));
    uint8_t kinds = 0;
    EXPECT_FALSE(saveditems::inspect(BOOK, kinds));
    EXPECT_EQ(kinds, saveditems::CROSSINK_BOOKMARKS);
  }
}

TEST(SavedItems, LargeLegacyFilesWarnWithoutReadingEveryRecord) {
  fake::reset();
  fake::add(legacyPath(), legacyData(5, 8));
  uint8_t kinds = 0;
  EXPECT_TRUE(saveditems::inspect(BOOK, kinds));
  EXPECT_EQ(kinds, saveditems::CROSSINK_BOOKMARKS);
  fake::add(legacyPath(), legacyData(5, 9));
  EXPECT_FALSE(saveditems::inspect(BOOK, kinds));
  EXPECT_EQ(kinds, saveditems::CROSSINK_BOOKMARKS);
}

TEST(SavedItems, ClippingCountValidatesContentsAndReadsRecoveryBackup) {
  fake::reset();
  addClipping();
  uint16_t count = 0;
  EXPECT_TRUE(ClippingStore::countForBook(BOOK, count));
  EXPECT_EQ(count, 1);
  const auto path = "/.crosspoint/clippings/epub_" + std::to_string(std::hash<std::string>{}(BOOK)) + ".bin";
  fake::files[path + ".bak"] = fake::files[path];
  fake::files.erase(path);
  EXPECT_TRUE(ClippingStore::countForBook(BOOK, count));
  EXPECT_EQ(count, 1);
  fake::files[path + ".bak"]->bytes.pop_back();
  EXPECT_FALSE(ClippingStore::countForBook(BOOK, count));
  EXPECT_EQ(count, 0);
}

TEST(SavedItems, BothLegacyFilesRemainVisibleAndReadable) {
  fake::reset();
  fake::add(legacyPath(), legacyData(5));
  fake::add(legacyPath(false), legacyData(3));
  CrossInkBookmarks bookmarks;
  ASSERT_TRUE(bookmarks.open(BOOK));
  EXPECT_EQ(bookmarks.count(), 4);
  CrossInkBookmarks::Entry entry;
  ASSERT_TRUE(bookmarks.read(0, entry));
  EXPECT_EQ(entry.paragraph, 3);
  ASSERT_TRUE(bookmarks.read(2, entry));
  EXPECT_EQ(entry.paragraph, UINT16_MAX);
  EXPECT_EQ(entry.spine, 1);
}

TEST(SavedItems, DamagedPrimaryDoesNotHideReadableLegacySource) {
  fake::reset();
  fake::add(legacyPath(), "bad");
  fake::add(legacyPath(false), legacyData(3));
  CrossInkBookmarks bookmarks;
  ASSERT_TRUE(bookmarks.open(BOOK));
  EXPECT_EQ(bookmarks.count(), 2);
  EXPECT_TRUE(bookmarks.unreadable());
  CrossInkBookmarks::Entry entry;
  EXPECT_TRUE(bookmarks.read(0, entry));
  uint8_t kinds = 0;
  EXPECT_FALSE(saveditems::inspect(BOOK, kinds));
  EXPECT_EQ(kinds, saveditems::CROSSINK_BOOKMARKS);
}

TEST(SavedItems, InterruptedLegacyEditShowsValidatedBackupReadOnly) {
  fake::reset();
  const auto backup = legacyPath() + ".bookmark-edit.bak";
  const auto original = legacyData(5);
  fake::add(backup, original);
  CrossInkBookmarks bookmarks;
  ASSERT_TRUE(bookmarks.open(BOOK));
  EXPECT_TRUE(bookmarks.unreadable());
  ASSERT_EQ(bookmarks.count(), 2);
  CrossInkBookmarks::Entry entry;
  ASSERT_TRUE(bookmarks.read(0, entry));
  EXPECT_TRUE(entry.recovered);
  EXPECT_EQ(entry.spine, 1);
  uint8_t kinds = 0;
  EXPECT_FALSE(saveditems::inspect(BOOK, kinds));
  EXPECT_EQ(kinds, saveditems::CROSSINK_BOOKMARKS);
  EXPECT_EQ(std::string(fake::files.at(backup)->bytes.begin(), fake::files.at(backup)->bytes.end()), original);
  EXPECT_TRUE(fake::writesByPath.empty());
}

TEST(SavedItems, LegacyBackupNeverOverridesExistingOrUncertainPrimary) {
  const auto backup = legacyPath() + ".bookmark-edit.bak";
  fake::reset();
  fake::add(legacyPath(), legacyData(3));
  fake::add(backup, legacyData(5));
  CrossInkBookmarks existing;
  ASSERT_TRUE(existing.open(BOOK));
  CrossInkBookmarks::Entry entry;
  ASSERT_TRUE(existing.read(0, entry));
  EXPECT_FALSE(entry.recovered);
  EXPECT_EQ(entry.paragraph, UINT16_MAX);
  EXPECT_FALSE(existing.unreadable());

  fake::reset();
  fake::add(backup, legacyData(5));
  fake::failOpenPath = legacyPath();
  CrossInkBookmarks uncertain;
  EXPECT_FALSE(uncertain.open(BOOK));
  EXPECT_TRUE(uncertain.unreadable());
  EXPECT_TRUE(fake::writesByPath.empty());
}

TEST(SavedItems, DamagedPrimaryShowsValidatedLegacyBackupReadOnly) {
  for (bool useGuard : {false, true}) {
    fake::reset();
    const auto primary = legacyPath();
    const auto backup = primary + (useGuard ? ".bookmark-edit.guard" : ".bookmark-edit.bak");
    const auto original = legacyData(5);
    fake::add(primary, "damaged");
    fake::add(backup, original);
    if (useGuard) fake::add(primary + ".bookmark-edit.bak", "damaged");
    CrossInkBookmarks bookmarks;
    ASSERT_TRUE(bookmarks.open(BOOK));
    EXPECT_TRUE(bookmarks.unreadable());
    ASSERT_EQ(bookmarks.count(), 2);
    CrossInkBookmarks::Entry entry;
    ASSERT_TRUE(bookmarks.read(0, entry));
    EXPECT_TRUE(entry.recovered);
    EXPECT_EQ(entry.spine, 1);
    EXPECT_EQ(std::string(fake::files.at(primary)->bytes.begin(), fake::files.at(primary)->bytes.end()), "damaged");
    EXPECT_EQ(std::string(fake::files.at(backup)->bytes.begin(), fake::files.at(backup)->bytes.end()), original);
    EXPECT_TRUE(fake::writesByPath.empty());
  }
}

TEST(SavedItems, LegacyBackupMustMatchBookPath) {
  for (bool damagedPrimary : {false, true}) {
    fake::reset();
    if (damagedPrimary) fake::add(legacyPath(), "damaged");
    fake::add(legacyPath() + ".bookmark-edit.bak", legacyData(5, 2, "/other.epub"));
    CrossInkBookmarks bookmarks;
    EXPECT_FALSE(bookmarks.open(BOOK));
    EXPECT_TRUE(bookmarks.unreadable());
    EXPECT_TRUE(fake::writesByPath.empty());
  }
}

TEST(SavedItems, LegacyBackupLookupFailureIsNotReportedAsEmpty) {
  fake::reset();
  fake::failOpenPath = legacyPath() + ".bookmark-edit.bak";
  CrossInkBookmarks bookmarks;
  EXPECT_FALSE(bookmarks.open(BOOK));
  EXPECT_TRUE(bookmarks.unreadable());
  EXPECT_TRUE(fake::writesByPath.empty());
}

TEST(SavedItems, InterruptedRenameCanReadGuardWithoutMutatingFiles) {
  for (bool damagedBackup : {false, true}) {
    fake::reset();
    const auto guard = legacyPath() + ".bookmark-edit.guard";
    const auto original = legacyData(5);
    fake::add(guard, original);
    if (damagedBackup) fake::add(legacyPath() + ".bookmark-edit.bak", "damaged");
    CrossInkBookmarks bookmarks;
    ASSERT_TRUE(bookmarks.open(BOOK));
    EXPECT_TRUE(bookmarks.unreadable());
    CrossInkBookmarks::Entry entry;
    ASSERT_TRUE(bookmarks.read(0, entry));
    EXPECT_TRUE(entry.recovered);
    EXPECT_EQ(entry.spine, 1);
    EXPECT_EQ(std::string(fake::files.at(guard)->bytes.begin(), fake::files.at(guard)->bytes.end()), original);
    EXPECT_TRUE(fake::writesByPath.empty());
  }
}
