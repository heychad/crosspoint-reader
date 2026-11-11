#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

#include "Epub.h"
#include "LibraryBuilder.h"
#include "LibraryIndexFile.h"

using namespace library;

namespace {

constexpr char INDEX[] = "/.crosspoint/library.idx";

std::string numbered(const char* prefix, const unsigned value) {
  char text[32];
  std::snprintf(text, sizeof(text), "%s%04u", prefix, value);
  return text;
}

std::string pathAt(LibraryIndexFile& index, const SortOrder order, const uint16_t row) {
  const uint16_t ordinal = index.ordinalForRow(order, row);
  if (ordinal == 0xFFFF) return {};
  ClixRecord record{};
  if (!index.readRecord(ordinal, record)) return {};
  std::string path;
  return index.readPath(record, path) ? path : std::string();
}

class LibraryBuilderTest : public ::testing::Test {
 protected:
  BuildStats stats;

  void SetUp() override {
    fake::reset();
    bookMetadata.clear();
    fake::add("/a.epub");
    fake::add("/b.epub");
  }

  void initial() { ASSERT_TRUE(buildLibraryIndex("/", stats, true)); }
};

}  // namespace

TEST(LibraryBookFileTest, AcceptsBookFilesTheWalkLists) {
  EXPECT_TRUE(isLibraryBookFile("book.epub"));
  EXPECT_TRUE(isLibraryBookFile("/Books/book.epub"));
  EXPECT_TRUE(isLibraryBookFile("/notes.txt"));
  EXPECT_TRUE(isLibraryBookFile("/a/b/c/readme.md"));
  EXPECT_TRUE(isLibraryBookFile("/Comics/issue.xtc"));
  EXPECT_TRUE(isLibraryBookFile("/Comics/issue.xtch"));
  EXPECT_TRUE(isLibraryBookFile("/Books/./book.epub"));
}

TEST(LibraryBookFileTest, RejectsOtherFilesAndHiddenNames) {
  EXPECT_FALSE(isLibraryBookFile(""));
  EXPECT_FALSE(isLibraryBookFile("/Books/book.pdf"));
  EXPECT_FALSE(isLibraryBookFile("/Books/book.epub.part"));
  EXPECT_FALSE(isLibraryBookFile("/.crosspoint/library.dirty"));
  EXPECT_FALSE(isLibraryBookFile("/.crosspoint/plugins/foo/README.md"));
  EXPECT_FALSE(isLibraryBookFile("/.plugins/foo/notes.txt"));
  EXPECT_FALSE(isLibraryBookFile("/Books/.hidden/book.epub"));
  EXPECT_FALSE(isLibraryBookFile("/Books/._book.epub"));
  EXPECT_FALSE(isLibraryBookFile("._book.epub"));
}

TEST(LibraryBookFileTest, RejectsSystemFoldersWithoutHidingOrdinaryTextBooks) {
  EXPECT_FALSE(isLibraryBookFile("/XTCache/fontPos.txt"));
  EXPECT_FALSE(isLibraryBookFile("/Backup/XTCache/epub/book/language.txt"));
  EXPECT_FALSE(isLibraryBookFile("/System Volume Information/notes.txt"));
  EXPECT_TRUE(isLibraryBookFile("/fontPos.txt"));
  EXPECT_TRUE(isLibraryBookFile("/language.txt"));
  EXPECT_TRUE(isLibraryBookFile("/XTCacheArchive/book.txt"));
}

TEST_F(LibraryBuilderTest, SkipsSystemFolderContents) {
  fake::add("/XTCache/fontPos.txt");
  fake::add("/XTCache/epub/book/language.txt");
  fake::add("/System Volume Information/notes.txt");
  fake::add("/fontPos.txt");
  fake::add("/language.txt");
  fake::add("/XTCacheArchive/book.txt");

  initial();

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().bookCount, 5u);
  EXPECT_EQ(fake::directoryEntriesByPath["/XTCache/fontPos.txt"], 0u);
  EXPECT_EQ(fake::directoryEntriesByPath["/XTCache/epub"], 0u);
  EXPECT_EQ(fake::directoryEntriesByPath["/System Volume Information/notes.txt"], 0u);
}

TEST(LibraryBookFileTest, RejectsResourceRootsButNotNestedNamesOrSubstrings) {
  for (const auto* root : {"fonts", "dictionaries", "plugins", "sleep", "sleep-overlay"}) {
    const std::string path = std::string("/") + root;
    EXPECT_FALSE(isLibraryBookFile(path + "/LICENSE.txt")) << path;
    EXPECT_FALSE(isLibraryBookFile(path + "/nested/book.epub")) << path;
    EXPECT_FALSE(isLibraryBookFile("/./" + path + "/README.md")) << path;
    EXPECT_TRUE(isLibraryBookFile(path + "-books/book.epub")) << path;
    EXPECT_TRUE(isLibraryBookFile(std::string("/books-") + root + "/book.epub")) << path;
    EXPECT_TRUE(isLibraryBookFile(std::string("/books-") + root + "-archive/book.epub")) << path;
    EXPECT_TRUE(isLibraryBookFile("/Books" + path + "/book.txt")) << path;
    EXPECT_TRUE(isLibraryBookFile(path + ".txt")) << path;
  }
  EXPECT_FALSE(isLibraryBookFile("/FoNtS/family/LICENSE.TXT"));
  EXPECT_FALSE(isLibraryBookFile("/DICTIONARIES/nested/book.epub"));
}

TEST(LibraryBookFileTest, ResourceRootsUseAsciiCaseInsensitiveMatching) {
  for (const auto* name : {"FoNtS", "DiCtIoNaRiEs", "PlUgInS", "SlEeP", "SlEeP-OvErLaY"}) {
    const std::string root = std::string("/") + name;
    EXPECT_FALSE(isLibraryBookFile(root + "/book.epub")) << root;
    EXPECT_TRUE(isLibraryBookFile("/books" + root + "/book.epub")) << root;
    EXPECT_TRUE(isLibraryBookFile(root + "-books/book.epub")) << root;
  }
  EXPECT_TRUE(isLibraryBookFile("/f\xC3\xB6nt/book.epub"));
  EXPECT_TRUE(isLibraryBookFile("/font\xC5\xBF/book.epub"));
}

TEST_F(LibraryBuilderTest, ScansNestedSkippedNamesAndRootSubstringNames) {
  std::vector<std::string> expected = {"/a.epub", "/b.epub"};
  expected.reserve(expected.size() + 5 * 4);
  for (const auto* name : {"fonts", "dictionaries", "plugins", "sleep", "sleep-overlay"}) {
    expected.push_back(std::string("/books/") + name + "/nested/book.epub");
    expected.push_back(std::string("/") + name + "-books/nested/book.epub");
    expected.push_back(std::string("/books-") + name + "/nested/book.epub");
    expected.push_back(std::string("/books-") + name + "-archive/nested/book.epub");
  }
  for (const auto& path : expected) fake::add(path);

  initial();

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  ASSERT_EQ(index.bookCount(), expected.size());
  std::vector<std::string> actual;
  actual.reserve(index.bookCount());
  for (uint16_t row = 0; row < index.bookCount(); row++) {
    actual.push_back(pathAt(index, SortOrder::TitleAsc, row));
  }
  std::sort(actual.begin(), actual.end());
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(actual, expected);
  for (const auto& path : expected) {
    EXPECT_GT(fake::directoryEntriesByPath[path], 0u) << path;
  }
}

TEST_F(LibraryBuilderTest, PrunesResourceRootsWithoutScanningTheirContents) {
  std::vector<std::string> expected = {"/a.epub", "/b.epub"};
  expected.reserve(expected.size() + 6 * 3);
  for (const auto* root : {"fonts", "dictionaries", "plugins", "sleep", "sleep-overlay", "FoNtS"}) {
    const std::string path = std::string("/") + root;
    fake::add(path + "/LICENSE.txt");
    fake::add(path + "/nested/book.epub");
    expected.push_back(path + "-books/book.epub");
    expected.push_back("/Books" + path + "/book.txt");
    expected.push_back(path + ".txt");
  }
  for (const auto& path : expected) fake::add(path);

  initial();

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  ASSERT_EQ(index.bookCount(), expected.size());
  std::vector<std::string> actual;
  actual.reserve(index.bookCount());
  for (uint16_t row = 0; row < index.bookCount(); row++) {
    actual.push_back(pathAt(index, SortOrder::TitleAsc, row));
  }
  std::sort(actual.begin(), actual.end());
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(actual, expected);
  for (const auto* root : {"fonts", "dictionaries", "plugins", "sleep", "sleep-overlay", "FoNtS"}) {
    const std::string path = std::string("/") + root;
    EXPECT_EQ(fake::directoryEntriesByPath[path + "/LICENSE.txt"], 0u) << path;
    EXPECT_EQ(fake::directoryEntriesByPath[path + "/nested"], 0u) << path;
    EXPECT_EQ(fake::directoryEntriesByPath[path + "/nested/book.epub"], 0u) << path;
  }
}

TEST_F(LibraryBuilderTest, RootAliasesApplyTheSameResourceExclusions) {
  for (const auto* alias : {"/", "//", "///", "/./", "/.//./", ".", "./", ".//."}) {
    SCOPED_TRACE(alias);
    fake::reset();
    EXPECT_TRUE(isLibraryBookFile(std::string(alias) + "/books/sleep/book.epub"));
    EXPECT_TRUE(isLibraryBookFile(std::string(alias) + "/sleep-books/book.epub"));
    EXPECT_FALSE(isLibraryBookFile(std::string(alias) + "/books/.hidden/book.epub"));
    fake::add("/books/sleep/book.epub");
    fake::add("/sleep-books/book.epub");
    for (const auto* name : {"fonts", "dictionaries", "plugins", "sleep", "sleep-overlay"}) {
      const std::string root = std::string("/") + name;
      fake::add(root + "/LICENSE.txt");
      fake::add(root + "/nested/book.epub");
      EXPECT_FALSE(isLibraryBookFile(std::string(alias) + "/" + name + "/LICENSE.txt"));
    }

    ASSERT_TRUE(buildLibraryIndex(alias, stats));

    LibraryIndexFile index;
    ASSERT_TRUE(index.open(INDEX));
    ASSERT_EQ(index.bookCount(), 2u);
    std::vector<std::string> actual;
    actual.reserve(index.bookCount());
    for (uint16_t row = 0; row < index.bookCount(); row++) {
      actual.push_back(pathAt(index, SortOrder::TitleAsc, row));
    }
    std::sort(actual.begin(), actual.end());
    EXPECT_EQ(actual, (std::vector<std::string>{"/books/sleep/book.epub", "/sleep-books/book.epub"}));
    for (const auto* name : {"fonts", "dictionaries", "plugins", "sleep", "sleep-overlay"}) {
      const std::string root = std::string("/") + name;
      EXPECT_EQ(fake::directoryEntriesByPath[root + "/LICENSE.txt"], 0u);
      EXPECT_EQ(fake::directoryEntriesByPath[root + "/nested"], 0u);
    }
  }
}

TEST_F(LibraryBuilderTest, SubRootScansSkipResourceFoldersAndTheirDescendants) {
  for (const auto* name : {"fonts", "dictionaries", "plugins", "sleep", "sleep-overlay", "FoNtS"}) {
    const std::string root = std::string("/") + name;
    for (const auto* alias : {"", "/", "/./"}) {
      for (const auto* suffix : {"", "/nested"}) {
        const std::string scanRoot = std::string(alias) + root + suffix;
        SCOPED_TRACE(scanRoot);
        fake::reset();
        fake::add(root + "/book.epub");
        fake::add(root + "/nested/book.txt");
        EXPECT_FALSE(isLibraryBookFile(scanRoot + "/book.epub"));

        ASSERT_TRUE(buildLibraryIndex(scanRoot.c_str(), stats));

        LibraryIndexFile index;
        ASSERT_TRUE(index.open(INDEX));
        EXPECT_EQ(index.bookCount(), 0u);
        EXPECT_EQ(fake::directoryEntriesByPath[root + "/book.epub"], 0u);
        EXPECT_EQ(fake::directoryEntriesByPath[root + "/nested"], 0u);
        EXPECT_EQ(fake::directoryEntriesByPath[root + "/nested/book.txt"], 0u);
      }
    }
  }
}

TEST_F(LibraryBuilderTest, SubRootScansKeepNestedAndSubstringResourceNames) {
  for (const auto* scanRoot : {"/books", "/books/fonts", "/sleep-books", "/books-sleep"}) {
    SCOPED_TRACE(scanRoot);
    fake::reset();
    const std::string bookPath = std::string(scanRoot) + "/sleep/book.epub";
    fake::add(bookPath);
    EXPECT_TRUE(isLibraryBookFile(bookPath));

    ASSERT_TRUE(buildLibraryIndex(scanRoot, stats));

    LibraryIndexFile index;
    ASSERT_TRUE(index.open(INDEX));
    ASSERT_EQ(index.bookCount(), 1u);
    EXPECT_EQ(pathAt(index, SortOrder::TitleAsc, 0), bookPath);
    EXPECT_GT(fake::directoryEntriesByPath[bookPath], 0u);
  }
}

TEST_F(LibraryBuilderTest, XtcTitleAndAuthorComeFromTheFile) {
  fake::add("/Comics/one.xtc");
  fake::add("/Comics/two.xtch");
  bookMetadata["/Comics/one.xtc"] = {"Go Problems", "Cho Chikun", true, {}, {}, {}};
  bookMetadata["/Comics/two.xtch"] = {"Sakata no Go", "Sakata Eio", true, {}, {}, {}};
  initial();

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  ASSERT_EQ(index.header().bookCount, 4u);
  bool sawXtc = false;
  bool sawXtch = false;
  for (uint16_t ordinal = 0; ordinal < index.header().bookCount; ordinal++) {
    ClixRecord record{};
    std::string path;
    std::string title;
    std::string author;
    ASSERT_TRUE(index.readRecord(ordinal, record));
    ASSERT_TRUE(index.readPath(record, path));
    if (path != "/Comics/one.xtc" && path != "/Comics/two.xtch") continue;
    ASSERT_TRUE(index.readTitle(record, title));
    ASSERT_TRUE(index.readAuthor(record, author));
    EXPECT_EQ(title, bookMetadata[path].title);
    EXPECT_EQ(author, bookMetadata[path].author);
    (path == "/Comics/one.xtc" ? sawXtc : sawXtch) = true;
  }
  EXPECT_TRUE(sawXtc);
  EXPECT_TRUE(sawXtch);
}

TEST_F(LibraryBuilderTest, UnchangedRebuildReusesMetadataAndDoesNotReplaceIndex) {
  initial();
  const auto old = fake::files[INDEX]->bytes;
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  EXPECT_EQ(fake::parses, 0u);
  EXPECT_EQ(stats.parsed, 0);
  EXPECT_EQ(stats.metadataReused, 2);
  EXPECT_FALSE(stats.indexReplaced);
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
}

TEST_F(LibraryBuilderTest, CoverageFlagTracksSkippedBooksAndUpgradesOlderIndexes) {
  initial();
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_NE(index.header().flags & CLIX_FLAG_SCAN_COMPLETE, 0);
  index.close();

  // An index written before the coverage flag must be rebuilt even when the
  // library contents are otherwise unchanged.
  auto& bytes = fake::files.at(INDEX)->bytes;
  ClixHeader old{};
  std::memcpy(&old, bytes.data(), sizeof(old));
  old.flags &= ~CLIX_FLAG_SCAN_COMPLETE;
  std::memcpy(bytes.data(), &old, sizeof(old));
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_TRUE(stats.indexReplaced);
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_NE(index.header().flags & CLIX_FLAG_SCAN_COMPLETE, 0);
  index.close();

  fake::add("/empty.epub", "");
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.unreadableSkipped, 1);
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().flags & CLIX_FLAG_SCAN_COMPLETE, 0);
  index.close();

  fake::add("/empty.epub", "book");
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_NE(index.header().flags & CLIX_FLAG_SCAN_COMPLETE, 0);
}

TEST_F(LibraryBuilderTest, FolderHeavyUnchangedReconciliationIoScalesLinearly) {
  const auto measure = [this](const unsigned count) {
    fake::reset();
    bookMetadata.clear();
    for (unsigned i = 0; i < count; i++) {
      fake::add("/folder" + numbered("", i) + "/book.txt");
    }
    if (!buildLibraryIndex("/", stats, false)) {
      ADD_FAILURE() << "initial build failed for " << count << " books";
      return 0u;
    }
    fake::resetIoCounters();
    if (!buildLibraryIndex("/", stats, false)) {
      ADD_FAILURE() << "unchanged build failed for " << count << " books";
      return 0u;
    }
    EXPECT_EQ(stats.metadataReused, count);
    EXPECT_FALSE(stats.indexReplaced);
    return fake::reads + fake::seeks;
  };

  const unsigned smallIo = measure(128);
  const unsigned largeIo = measure(256);
  EXPECT_LT(largeIo, smallIo * 3u);
}

TEST_F(LibraryBuilderTest, DirectoryEntriesAreEnumeratedOnce) {
  fake::add("/folder/c.txt");

  ASSERT_TRUE(buildLibraryIndex("/", stats, false));

  EXPECT_EQ(fake::directoryEntriesByPath["/a.epub"], 1u);
  EXPECT_EQ(fake::directoryEntriesByPath["/b.epub"], 1u);
  EXPECT_EQ(fake::directoryEntriesByPath["/folder"], 1u);
  EXPECT_EQ(fake::directoryEntriesByPath["/folder/c.txt"], 1u);
}

TEST_F(LibraryBuilderTest, DirectoryResumeFailureRetainsPreviousIndex) {
  initial();
  const auto old = fake::files[INDEX]->bytes;
  fake::add("/aa-folder/c.txt");
  fake::failDirectorySeek = true;

  EXPECT_FALSE(buildLibraryIndex("/", stats, false));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
}

TEST_F(LibraryBuilderTest, StagingAndIndexWritesAreBatched) {
  fake::reset();
  for (unsigned i = 0; i < 128; i++) fake::add("/book" + numbered("", i) + ".txt");

  ASSERT_TRUE(buildLibraryIndex("/", stats, false));

  EXPECT_LT(fake::writesByPath["/.crosspoint/library.stage"], 64u);
  EXPECT_LT(fake::writesByPath["/.crosspoint/library.new"], 32u);
}

TEST_F(LibraryBuilderTest, ParentDuplicateTrackingSurvivesDirectoryRecursion) {
  fake::add("/folder/c.txt");
  fake::duplicateDirectoryEntry("/a.epub");

  ASSERT_TRUE(buildLibraryIndex("/", stats, false));

  EXPECT_EQ(stats.books, 3);
  EXPECT_EQ(stats.duplicatesDropped, 1);
}

TEST_F(LibraryBuilderTest, TimestampAndSizeChangesParseOnlyTheChangedBook) {
  initial();
  fake::files["/a.epub"]->time++;
  fake::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 1u);
  EXPECT_EQ(stats.metadataReused, 1);

  fake::files["/b.epub"]->bytes.push_back('x');
  fake::parses = 0;
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 1u);
  EXPECT_EQ(stats.metadataReused, 1);
}

TEST_F(LibraryBuilderTest, ZeroTimestampAndFailedExtractionAreNeverFresh) {
  fake::files["/a.epub"]->time = 0;
  bookMetadata["/b.epub"].success = false;
  initial();
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  EXPECT_EQ(fake::parses, 2u);
  EXPECT_EQ(stats.metadataReused, 0);
  EXPECT_TRUE(stats.indexReplaced);
}

TEST_F(LibraryBuilderTest, MetadataModeChangesInvalidateCachedMetadata) {
  initial();
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_EQ(fake::parses, 0u);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().metadataEnabled, 0);
  index.close();

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::parses, 2u);
}

TEST_F(LibraryBuilderTest, RebuildVotesFromSourceAuthorInsteadOfPriorCanonicalAuthor) {
  fake::add("/c.epub");
  bookMetadata["/a.epub"].author = "Victor Hugo";
  bookMetadata["/b.epub"].author = "Hugo Victor";
  bookMetadata["/c.epub"].author = "Hugo Victor";
  initial();
  ASSERT_TRUE(Storage.remove("/b.epub"));
  ASSERT_TRUE(Storage.remove("/c.epub"));
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  EXPECT_EQ(fake::parses, 0u);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  ClixRecord record{};
  std::string author;
  ASSERT_TRUE(index.readRecord(0, record));
  ASSERT_TRUE(index.readAuthor(record, author));
  EXPECT_EQ(author, "Victor Hugo");
}

TEST_F(LibraryBuilderTest, EqualBasenamesInDifferentFoldersReconcileIndependently) {
  fake::add("/one/same.epub");
  fake::add("/two/same.epub");
  bookMetadata["/one/same.epub"].title = "One";
  bookMetadata["/two/same.epub"].title = "Two";
  initial();
  fake::files["/two/same.epub"]->time++;
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  EXPECT_EQ(fake::parses, 1u);
  EXPECT_EQ(stats.metadataReused, 3);
}

TEST_F(LibraryBuilderTest, ArrivalOrderFollowsModificationTimeOverDiscoveryOrder) {
  // a and b exist with the default time; c lands with an older timestamp and d
  // with the newest, so file times, not walk or firstSeen order, decide.
  fake::add("/c.epub", "book c", /*time=*/0);
  fake::add("/d.epub", "book d", /*time=*/9);
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 0), "/c.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 1), "/a.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 2), "/b.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 3), "/d.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentDesc, 0), "/d.epub");
}

TEST_F(LibraryBuilderTest, AddedRemovedMovedAndRenamedBooksKeepArrivalOrder) {
  initial();
  fake::add("/c.epub");
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 0), "/a.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 1), "/b.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 2), "/c.epub");
  index.close();

  ASSERT_TRUE(Storage.remove("/b.epub"));
  ASSERT_TRUE(Storage.rename("/a.epub", "/moved.epub"));
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.removed, 1);
  EXPECT_EQ(stats.renamed, 1);
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 0), "/moved.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 1), "/c.epub");
  index.close();

  ASSERT_TRUE(Storage.rename("/moved.epub", "/renamed.epub"));
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.renamed, 1);
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 0), "/renamed.epub");
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 1), "/c.epub");
}

TEST_F(LibraryBuilderTest, WholeFolderRenameWithUniqueSizePreservesArrivalOrder) {
  fake::add("/old/unique.epub", "a uniquely sized book");
  initial();
  ASSERT_TRUE(Storage.mkdir("/new"));
  ASSERT_TRUE(Storage.rename("/old/unique.epub", "/new/unique.epub"));
  fake::parses = 0;

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));

  EXPECT_EQ(stats.renamed, 1);
  EXPECT_EQ(stats.removed, 0);
  EXPECT_EQ(fake::parses, 1u);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, 2), "/new/unique.epub");
}

TEST_F(LibraryBuilderTest, DuplicateDetectionRemainsBoundedAndFindsTrackedKeysAfterTheCap) {
  fake::duplicateDirectoryEntry("/a.epub");
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.books, 2);
  EXPECT_EQ(stats.duplicatesDropped, 1);
  EXPECT_FALSE(stats.dedupDegraded);

  fake::reset();
  bookMetadata.clear();
  for (unsigned i = 0; i <= LIBRARY_MAX_DEDUP_KEYS; i++) {
    fake::add("/book" + numbered("", i) + ".txt");
  }
  fake::duplicateDirectoryEntry("/book0000.txt");
  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_EQ(stats.books, LIBRARY_MAX_DEDUP_KEYS + 1);
  EXPECT_EQ(stats.duplicatesDropped, 1);
  EXPECT_TRUE(stats.dedupDegraded);
  EXPECT_LT(fake::delays, 2000u);
}

TEST_F(LibraryBuilderTest, ReadWriteCloseAndAllocationFailuresRetainPreviousIndex) {
  initial();
  const auto old = fake::files[INDEX]->bytes;

  fake::failRead = 0;
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
  fake::failRead = -1;

  fake::files["/a.epub"]->time++;
  fake::failWrite = 0;
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
  fake::failWrite = -1;

  fake::failWritePath = "/.crosspoint/library.new";
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);

  fake::failClosePath = "/.crosspoint/library.new";
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);

  fake::failAlloc = 3;
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
  fake::failAlloc = -1;

  fake::failRename = 1;
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, old);
}

TEST_F(LibraryBuilderTest, TruncatedPersistedPathHashAbortsAndRetainsTheLiveIndex) {
  initial();
  auto& bytes = fake::files[INDEX]->bytes;
  ClixHeader header{};
  std::memcpy(&header, bytes.data(), sizeof(header));
  ClixRecord record{};
  std::memcpy(&record, bytes.data() + recordOffset(header, 0), sizeof(record));
  record.nameOff = header.nameLen - 4;
  std::memcpy(bytes.data() + recordOffset(header, 0), &record, sizeof(record));
  const auto corrupted = bytes;

  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files[INDEX]->bytes, corrupted);
  EXPECT_FALSE(Storage.exists("/.crosspoint/library.stage"));
  EXPECT_FALSE(Storage.exists("/.crosspoint/library.stage.f"));
}

TEST_F(LibraryBuilderTest, LibrariesPastOldGateAndAtFormatCeilingKeepAllOrders) {
  for (const unsigned count : {513u, static_cast<unsigned>(CLIX_MAX_RECORDS)}) {
    fake::reset();
    bookMetadata.clear();
    std::vector<unsigned> authorOrder(count);
    std::iota(authorOrder.begin(), authorOrder.end(), 0u);
    for (unsigned i = 0; i < count; i++) {
      const std::string path = "/book" + numbered("", i) + ".epub";
      fake::add(path);
      bookMetadata[path].title = numbered("Title ", count - 1 - i);
      bookMetadata[path].author = numbered("Writer ", (i * (count == 513 ? 257u : 2053u)) % count);
    }

    ASSERT_TRUE(buildLibraryIndex("/", stats, true)) << count;
    ASSERT_EQ(stats.books, count);
    EXPECT_FALSE(stats.ranksDegraded);

    if (count == CLIX_MAX_RECORDS) {
      const auto old = fake::files[INDEX]->bytes;
      fake::parses = 0;
      fake::resetIoCounters();
      ASSERT_TRUE(buildLibraryIndex("/", stats, true));
      EXPECT_EQ(fake::parses, 0u);
      EXPECT_EQ(stats.metadataReused, CLIX_MAX_RECORDS);
      EXPECT_FALSE(stats.indexReplaced);
      EXPECT_EQ(fake::files[INDEX]->bytes, old);
      EXPECT_LT(fake::delays, 10000u);
    }

    std::sort(authorOrder.begin(), authorOrder.end(), [count](const unsigned a, const unsigned b) {
      return (a * (count == 513 ? 257u : 2053u)) % count < (b * (count == 513 ? 257u : 2053u)) % count;
    });
    LibraryIndexFile index;
    ASSERT_TRUE(index.open(INDEX));
    for (uint16_t row = 0; row < count; row++) {
      EXPECT_EQ(pathAt(index, SortOrder::RecentAsc, row), "/book" + numbered("", row) + ".epub") << count << ':' << row;
      EXPECT_EQ(pathAt(index, SortOrder::TitleAsc, row), "/book" + numbered("", count - 1 - row) + ".epub")
          << count << ':' << row;
      EXPECT_EQ(pathAt(index, SortOrder::AuthorAsc, row), "/book" + numbered("", authorOrder[row]) + ".epub")
          << count << ':' << row;
    }
  }
}

TEST_F(LibraryBuilderTest, SortAllocationFailureProducesValidDegradedIndex) {
  fake::reset();
  for (unsigned i = 0; i < 513; i++) fake::add("/book" + numbered("", i) + ".txt");
  fake::failAlloc = 6;

  ASSERT_TRUE(buildLibraryIndex("/", stats, false));
  EXPECT_TRUE(fake::failureTriggered);
  EXPECT_TRUE(stats.ranksDegraded);
  EXPECT_TRUE(stats.indexReplaced);

  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.bookCount(), 513);
}

TEST_F(LibraryBuilderTest, SortsFullGenresAndSeriesNumbersAndReusesStoredMetadata) {
  fake::reset();
  bookMetadata.clear();
  const std::vector<std::string> names{"ten", "two", "half", "negative", "missing", "other"};
  const std::vector<std::optional<float>> numbers{10.0f, 2.0f, 2.5f, -1.0f, std::nullopt, 1.0f};
  for (size_t i = 0; i < names.size(); ++i) {
    const std::string path = "/" + names[i] + ".epub";
    fake::add(path);
    auto& metadata = bookMetadata[path];
    metadata.title = names[i];
    metadata.series = i == 5 ? "Shared series prefix B" : "Shared series prefix A";
    metadata.seriesIndex = numbers[i];
    metadata.genre = i % 2 == 0 ? "Science Fiction / Space" : "Science Fiction / Adventure";
  }
  initial();
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  const std::vector<std::string> expected{"negative", "two", "half", "ten", "missing", "other"};
  for (uint16_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(pathAt(index, SortOrder::SeriesAsc, i), "/" + expected[i] + ".epub");
    EXPECT_EQ(pathAt(index, SortOrder::SeriesDesc, i), "/" + expected[expected.size() - 1 - i] + ".epub");
  }
  std::string previousGenre;
  for (uint16_t i = 0; i < names.size(); ++i) {
    ClixRecord record{};
    ASSERT_TRUE(index.readRecord(index.ordinalForRow(SortOrder::GenreAsc, i), record));
    std::string genre;
    ASSERT_TRUE(index.readGenre(record, genre));
    EXPECT_LE(previousGenre, genre);
    previousGenre = genre;
  }
  index.close();
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.metadataReused, names.size());
  EXPECT_EQ(stats.parsed, 0u);
  EXPECT_FALSE(stats.indexReplaced);
}

TEST_F(LibraryBuilderTest, VersionTwoRebuildPreservesArrivalIdentityAndExtractsRichMetadata) {
  bookMetadata["/a.epub"].series = "Series A";
  bookMetadata["/a.epub"].genre = "Adventure";
  initial();
  auto& bytes = fake::files.at(INDEX)->bytes;
  ClixHeader header{};
  memcpy(&header, bytes.data(), sizeof(header));
  const uint32_t blobStart = header.nameStart;
  header.formatVersion = CLIX_PREVIOUS_FORMAT_VERSION;
  layoutSections(header, header.folderLen, header.nameLen);
  ASSERT_EQ(header.nameStart, blobStart);
  memcpy(bytes.data(), &header, sizeof(header));
  LibraryIndexFile previous;
  ASSERT_FALSE(previous.open(INDEX));
  ASSERT_TRUE(previous.openForReconciliation(INDEX));
  ClixRecord before{};
  ASSERT_TRUE(previous.readRecord(0, before));
  previous.close();

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.unchanged, 2u);
  EXPECT_EQ(stats.parsed, 2u);
  EXPECT_TRUE(stats.indexReplaced);
  LibraryIndexFile rebuilt;
  ASSERT_TRUE(rebuilt.open(INDEX));
  ClixRecord after{};
  ASSERT_TRUE(rebuilt.readRecord(0, after));
  EXPECT_EQ(before.firstSeen, after.firstSeen);
  std::string genre;
  ASSERT_TRUE(rebuilt.readGenre(after, genre));
  EXPECT_EQ(genre, "Adventure");
}

TEST_F(LibraryBuilderTest, MigratesVersionTwoWithDifferentSectionOffsetsAndRetriesFailedWrite) {
  for (unsigned i = 0; i < 200; ++i) fake::add("/" + numbered("legacy", i) + ".epub");
  initial();
  auto& current = fake::files.at(INDEX)->bytes;
  ClixHeader header{};
  memcpy(&header, current.data(), sizeof(header));
  const uint32_t currentBlob = header.nameStart;
  header.formatVersion = CLIX_PREVIOUS_FORMAT_VERSION;
  layoutSections(header, header.folderLen, header.nameLen);
  ASSERT_LT(header.nameStart, currentBlob);
  std::vector<uint8_t> legacy(header.selfSize);
  std::copy_n(current.begin(), header.nameStart, legacy.begin());
  std::copy_n(current.begin() + currentBlob, header.nameLen, legacy.begin() + header.nameStart);
  memcpy(legacy.data(), &header, sizeof(header));
  current = legacy;
  LibraryIndexFile previous;
  ASSERT_TRUE(previous.openForReconciliation(INDEX));
  std::vector<uint16_t> arrivals;
  for (uint16_t i = 0; i < previous.bookCount(); ++i) {
    ClixRecord record{};
    ASSERT_TRUE(previous.readRecord(i, record));
    arrivals.push_back(record.firstSeen);
  }
  previous.close();

  fake::failWritePath = "/.crosspoint/library.new";
  EXPECT_FALSE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(fake::files.at(INDEX)->bytes, legacy);
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_EQ(stats.unchanged, arrivals.size());
  LibraryIndexFile rebuilt;
  ASSERT_TRUE(rebuilt.open(INDEX));
  for (uint16_t i = 0; i < rebuilt.bookCount(); ++i) {
    ClixRecord record{};
    ASSERT_TRUE(rebuilt.readRecord(i, record));
    EXPECT_EQ(record.firstSeen, arrivals[i]);
  }
}

TEST_F(LibraryBuilderTest, EmptyLegacyIndexIsMigratedInsteadOfReused) {
  fake::reset();
  bookMetadata.clear();
  initial();
  auto& bytes = fake::files.at(INDEX)->bytes;
  ClixHeader header{};
  memcpy(&header, bytes.data(), sizeof(header));
  header.formatVersion = CLIX_PREVIOUS_FORMAT_VERSION;
  memcpy(bytes.data(), &header, sizeof(header));
  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  EXPECT_TRUE(stats.indexReplaced);
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.bookCount(), 0u);
}

TEST_F(LibraryBuilderTest, RecoversValidLegacyBackupBeforeRebuildingInvalidLiveIndex) {
  initial();
  auto& bytes = fake::files.at(INDEX)->bytes;
  ClixHeader header{};
  memcpy(&header, bytes.data(), sizeof(header));
  header.formatVersion = CLIX_PREVIOUS_FORMAT_VERSION;
  header.nextFirstSeen = 77;
  memcpy(bytes.data(), &header, sizeof(header));
  for (uint16_t i = 0; i < header.bookCount; ++i) {
    const uint16_t firstSeen = 40 + i;
    memcpy(bytes.data() + recordOffset(header, i) + offsetof(ClixRecord, firstSeen), &firstSeen, sizeof(firstSeen));
  }
  ASSERT_TRUE(Storage.rename(INDEX, "/.crosspoint/library.bak"));
  fake::add(INDEX, std::string(sizeof(ClixHeader), '?'));

  ASSERT_TRUE(buildLibraryIndex("/", stats, true));
  LibraryIndexFile index;
  ASSERT_TRUE(index.open(INDEX));
  EXPECT_EQ(index.header().nextFirstSeen, 77u);
  for (uint16_t i = 0; i < index.bookCount(); ++i) {
    ClixRecord record{};
    ASSERT_TRUE(index.readRecord(i, record));
    EXPECT_EQ(record.firstSeen, 40 + i);
  }
}
