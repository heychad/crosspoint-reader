#include <CrossInkBookmarkEdit.h>
#include <CrossInkBookmarkMove.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace cm = readingstats::completionmove;
namespace {
void put32(std::vector<uint8_t>& data, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) data.push_back(static_cast<uint8_t>(value >> (i * 8)));
}
void putString(std::vector<uint8_t>& data, const std::string& value) {
  put32(data, value.size());
  data.insert(data.end(), value.begin(), value.end());
}
std::vector<uint8_t> fixture(uint8_t version) {
  std::vector<uint8_t> data{version, 1};
  if (version >= 3) data.push_back(0);
  putString(data, "A title");
  putString(data, "An author");
  putString(data, "/Authors/Writer/Book.epub");
  data.resize(data.size() + 58 + (version >= 4 ? 2 : 0) + (version >= 5 ? 64 : 0), 0x42);
  data.push_back(0x7f);  // Preserve unknown trailing bytes too.
  return data;
}
void store(const std::vector<uint8_t>& bytes) {
  HalFile out;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/bookmarks", out));
  ASSERT_EQ(out.write(bytes.data(), bytes.size()), bytes.size());
  ASSERT_TRUE(out.close());
}
}  // namespace

TEST(CrossInkBookmarkMove, ValidatesAllSupportedVersionsAndPreservesLayoutBoundaries) {
  for (uint8_t version = 2; version <= 5; ++version) {
    Storage.reset();
    const auto bytes = fixture(version);
    store(bytes);
    HalFile file;
    ASSERT_TRUE(Storage.openFileForRead("TEST", "/bookmarks", file));
    cm::BookmarkRewrite rewrite;
    ASSERT_EQ(cm::inspectBookmarkRewrite(file, "/Authors/Writer/Book.epub", "/read/Book.epub", rewrite),
              cm::BookmarkRewriteResult::Ready);
    EXPECT_EQ(rewrite.suffixOffset, rewrite.lengthOffset + 4 + std::string("/Authors/Writer/Book.epub").size());
    EXPECT_EQ(rewrite.outputSize,
              bytes.size() - std::string("/Authors/Writer/Book.epub").size() + std::string("/read/Book.epub").size());
    EXPECT_TRUE(file.close());
  }
}

TEST(CrossInkBookmarkMove, RejectsWrongBookAndTruncatedRecords) {
  Storage.reset();
  auto bytes = fixture(5);
  store(bytes);
  HalFile file;
  ASSERT_TRUE(Storage.openFileForRead("TEST", "/bookmarks", file));
  cm::BookmarkRewrite rewrite{3, 4, 5};
  EXPECT_EQ(cm::inspectBookmarkRewrite(file, "/different.epub", "/read/Book.epub", rewrite),
            cm::BookmarkRewriteResult::Invalid);
  EXPECT_EQ(rewrite.lengthOffset, 3u);
  EXPECT_TRUE(file.close());
  bytes.resize(bytes.size() - 3);
  store(bytes);
  ASSERT_TRUE(Storage.openFileForRead("TEST", "/bookmarks", file));
  EXPECT_EQ(cm::inspectBookmarkRewrite(file, "/Authors/Writer/Book.epub", "/read/Book.epub", rewrite),
            cm::BookmarkRewriteResult::Invalid);
  EXPECT_TRUE(file.close());
}

TEST(CrossInkBookmarkMove, StreamsPathRewriteWithoutChangingOtherBytes) {
  const std::string oldPath = "/Authors/Writer/Book.epub";
  const std::string newPath = "/read/Book.epub";
  for (uint8_t version = 2; version <= 5; ++version) {
    Storage.reset();
    const auto bytes = fixture(version);
    store(bytes);
    HalFile input, staging;
    ASSERT_TRUE(Storage.openFileForRead("TEST", "/bookmarks", input));
    ASSERT_TRUE(Storage.openFileForWrite("TEST", "/staging", staging));
    cm::BookmarkRewrite rewrite;
    ASSERT_EQ(cm::inspectBookmarkRewrite(input, oldPath, newPath, rewrite), cm::BookmarkRewriteResult::Ready);
    ASSERT_EQ(cm::writeBookmarkRewrite(input, staging, oldPath, newPath), cm::BookmarkRewriteResult::Ready);
    ASSERT_TRUE(input.close());
    ASSERT_TRUE(staging.close());
    auto expected = std::vector<uint8_t>(bytes.begin(), bytes.begin() + rewrite.lengthOffset);
    putString(expected, newPath);
    expected.insert(expected.end(), bytes.begin() + rewrite.suffixOffset, bytes.end());
    HalFile result;
    ASSERT_TRUE(Storage.openFileForRead("TEST", "/staging", result));
    std::vector<uint8_t> actual(result.fileSize());
    ASSERT_EQ(result.read(actual.data(), actual.size()), static_cast<int>(actual.size()));
    EXPECT_EQ(actual, expected);
    EXPECT_TRUE(result.close());
  }
}

TEST(CrossInkBookmarkMove, PreservesOriginalWhenStagingWriteFails) {
  Storage.reset();
  const auto bytes = fixture(5);
  store(bytes);
  Storage.failNextWriteFor("/staging", 10);
  HalFile input, staging;
  ASSERT_TRUE(Storage.openFileForRead("TEST", "/bookmarks", input));
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/staging", staging));
  EXPECT_EQ(cm::writeBookmarkRewrite(input, staging, "/Authors/Writer/Book.epub", "/read/Book.epub"),
            cm::BookmarkRewriteResult::Unavailable);
  EXPECT_TRUE(input.close());
  EXPECT_TRUE(staging.close());
  Storage.allowWritesFor("/staging");
  ASSERT_TRUE(Storage.openFileForRead("TEST", "/bookmarks", input));
  staging = Storage.openChecked("/staging", O_RDWR);
  ASSERT_TRUE(staging);
  bool complete = true;
  EXPECT_EQ(cm::compareBookmarkRewrite(input, staging, "/Authors/Writer/Book.epub", "/read/Book.epub", complete),
            cm::BookmarkRewriteResult::Ready);
  EXPECT_FALSE(complete);
  EXPECT_EQ(cm::writeBookmarkRewrite(input, staging, "/Authors/Writer/Book.epub", "/read/Book.epub"),
            cm::BookmarkRewriteResult::Ready);
  EXPECT_EQ(cm::compareBookmarkRewrite(input, staging, "/Authors/Writer/Book.epub", "/read/Book.epub", complete),
            cm::BookmarkRewriteResult::Ready);
  EXPECT_TRUE(complete);
  EXPECT_TRUE(input.close());
  EXPECT_TRUE(staging.close());
  HalFile original;
  ASSERT_TRUE(Storage.openFileForRead("TEST", "/bookmarks", original));
  std::vector<uint8_t> after(original.fileSize());
  ASSERT_EQ(original.read(after.data(), after.size()), static_cast<int>(after.size()));
  EXPECT_EQ(after, bytes);
  EXPECT_TRUE(original.close());
}

TEST(CrossInkBookmarkMove, RejectsMismatchedStagingPrefix) {
  Storage.reset();
  store(fixture(5));
  HalFile input, staging;
  ASSERT_TRUE(Storage.openFileForRead("TEST", "/bookmarks", input));
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/staging", staging));
  const uint8_t wrong[] = {0xff, 0xaa};
  ASSERT_EQ(staging.write(wrong, sizeof(wrong)), sizeof(wrong));
  EXPECT_EQ(cm::writeBookmarkRewrite(input, staging, "/Authors/Writer/Book.epub", "/read/Book.epub"),
            cm::BookmarkRewriteResult::Invalid);
  EXPECT_EQ(staging.fileSize(), sizeof(wrong));
  EXPECT_TRUE(staging.close());
  EXPECT_TRUE(input.close());
}

TEST(CrossInkBookmarkMove, RemovalPreservesOtherRecordsAndUnknownTrailingBytes) {
  const std::string book = "/Authors/Writer/Book.epub";
  for (uint8_t version = 2; version <= 5; ++version) {
    Storage.reset();
    auto bytes = fixture(version);
    const size_t recordSize = 58 + (version >= 4 ? 2 : 0) + (version >= 5 ? 64 : 0);
    const size_t recordStart = bytes.size() - recordSize - 1;
    bytes[1] = 3;
    bytes.insert(bytes.end() - 1, recordSize, 0x55);
    bytes.insert(bytes.end() - 1, recordSize, 0x66);
    store(bytes);
    HalFile input, staging;
    ASSERT_TRUE(Storage.openFileForRead("TEST", "/bookmarks", input));
    ASSERT_TRUE(Storage.openFileForWrite("TEST", "/staging", staging));
    ASSERT_EQ(cm::writeBookmarkRemoval(input, staging, book, 1, 1), cm::BookmarkRewriteResult::Ready);
    bool complete = false;
    ASSERT_EQ(cm::compareBookmarkRemoval(input, staging, book, 1, 1, complete), cm::BookmarkRewriteResult::Ready);
    EXPECT_TRUE(complete);
    EXPECT_EQ(cm::writeBookmarkRemoval(input, staging, book, 3, 1), cm::BookmarkRewriteResult::Invalid);
    ASSERT_TRUE(staging.close());
    ASSERT_TRUE(input.close());
    auto expected = bytes;
    expected[1] = 2;
    expected.erase(expected.begin() + recordStart + recordSize, expected.begin() + recordStart + 2 * recordSize);
    ASSERT_TRUE(Storage.openFileForRead("TEST", "/staging", staging));
    std::vector<uint8_t> actual(staging.fileSize());
    ASSERT_EQ(staging.read(actual.data(), actual.size()), static_cast<int>(actual.size()));
    EXPECT_EQ(actual, expected);
    EXPECT_TRUE(staging.close());
  }
}

TEST(CrossInkBookmarkMove, RemovalResumesOnlyMatchingStageAfterWriteFailure) {
  const std::string book = "/Authors/Writer/Book.epub";
  Storage.reset();
  const auto original = fixture(5);
  store(original);
  Storage.failNextWriteFor("/staging", 10);
  HalFile input, staging;
  ASSERT_TRUE(Storage.openFileForRead("TEST", "/bookmarks", input));
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/staging", staging));
  EXPECT_EQ(cm::writeBookmarkRemoval(input, staging, book, 0, 1), cm::BookmarkRewriteResult::Unavailable);
  ASSERT_TRUE(staging.close());
  Storage.allowWritesFor("/staging");
  staging = Storage.openChecked("/staging", O_RDWR);
  ASSERT_TRUE(staging);
  bool complete = true;
  ASSERT_EQ(cm::compareBookmarkRemoval(input, staging, book, 0, 1, complete), cm::BookmarkRewriteResult::Ready);
  EXPECT_FALSE(complete);
  ASSERT_EQ(cm::writeBookmarkRemoval(input, staging, book, 0, 1), cm::BookmarkRewriteResult::Ready);
  ASSERT_EQ(cm::compareBookmarkRemoval(input, staging, book, 0, 1, complete), cm::BookmarkRewriteResult::Ready);
  EXPECT_TRUE(complete);
  ASSERT_TRUE(input.close());
  ASSERT_TRUE(staging.close());
  ASSERT_TRUE(Storage.openFileForRead("TEST", "/bookmarks", input));
  std::vector<uint8_t> after(input.fileSize());
  ASSERT_EQ(input.read(after.data(), after.size()), static_cast<int>(after.size()));
  EXPECT_EQ(after, original);
  EXPECT_TRUE(input.close());
}

TEST(CrossInkBookmarkMove, RemovalRejectsAChangedStage) {
  const std::string book = "/Authors/Writer/Book.epub";
  Storage.reset();
  store(fixture(4));
  HalFile input, staging;
  ASSERT_TRUE(Storage.openFileForRead("TEST", "/bookmarks", input));
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/staging", staging));
  const uint8_t wrong[] = {0xff, 0x00};
  ASSERT_EQ(staging.write(wrong, sizeof(wrong)), sizeof(wrong));
  bool complete = true;
  EXPECT_EQ(cm::compareBookmarkRemoval(input, staging, book, 0, 1, complete), cm::BookmarkRewriteResult::Invalid);
  EXPECT_TRUE(complete);  // Failure must not change the caller's result.
  EXPECT_EQ(cm::writeBookmarkRemoval(input, staging, book, 0, 1), cm::BookmarkRewriteResult::Invalid);
  EXPECT_EQ(staging.fileSize(), sizeof(wrong));
  EXPECT_TRUE(staging.close());
  EXPECT_TRUE(input.close());
}

TEST(CrossInkBookmarkMove, EditPublishesOnlyVerifiedStageAndRetainsOriginal) {
  const std::string book = "/Authors/Writer/Book.epub";
  Storage.reset();
  const auto original = fixture(5);
  store(original);
  ASSERT_EQ(legacybookmarkedit::stage("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Ready);
  ASSERT_EQ(legacybookmarkedit::publish("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Published);
  EXPECT_FALSE(Storage.exists("/bookmarks.bookmark-edit.tmp"));
  EXPECT_TRUE(Storage.exists("/bookmarks.bookmark-edit.bak"));
  HalFile backup;
  ASSERT_TRUE(Storage.openFileForRead("TEST", "/bookmarks.bookmark-edit.bak", backup));
  std::vector<uint8_t> retained(backup.fileSize());
  ASSERT_EQ(backup.read(retained.data(), retained.size()), static_cast<int>(retained.size()));
  EXPECT_EQ(retained, original);
  EXPECT_TRUE(backup.close());
  EXPECT_EQ(legacybookmarkedit::stage("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Conflict);
}

TEST(CrossInkBookmarkMove, InterruptedEditPreservesRecoveryArtifacts) {
  const std::string book = "/Authors/Writer/Book.epub";
  Storage.reset();
  const auto original = fixture(5);
  store(original);
  ASSERT_EQ(legacybookmarkedit::stage("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Ready);
  Storage.failNextRenameFrom("/bookmarks.bookmark-edit.tmp");
  EXPECT_EQ(legacybookmarkedit::publish("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Uncertain);
  EXPECT_FALSE(Storage.exists("/bookmarks"));
  EXPECT_TRUE(Storage.exists("/bookmarks.bookmark-edit.tmp"));
  EXPECT_TRUE(Storage.exists("/bookmarks.bookmark-edit.bak"));
  HalFile backup;
  ASSERT_TRUE(Storage.openFileForRead("TEST", "/bookmarks.bookmark-edit.bak", backup));
  std::vector<uint8_t> retained(backup.fileSize());
  ASSERT_EQ(backup.read(retained.data(), retained.size()), static_cast<int>(retained.size()));
  EXPECT_EQ(retained, original);
  EXPECT_TRUE(backup.close());
  EXPECT_EQ(legacybookmarkedit::publish("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Conflict);
  EXPECT_EQ(legacybookmarkedit::restoreOriginal("/bookmarks", "/different.epub"), legacybookmarkedit::Result::Invalid);
  EXPECT_FALSE(Storage.exists("/bookmarks"));
  EXPECT_TRUE(Storage.exists("/bookmarks.bookmark-edit.bak"));
  ASSERT_EQ(legacybookmarkedit::restoreOriginal("/bookmarks", book), legacybookmarkedit::Result::Restored);
  EXPECT_TRUE(Storage.exists("/bookmarks.bookmark-edit.tmp"));
  EXPECT_FALSE(Storage.exists("/bookmarks.bookmark-edit.bak"));
  HalFile restored;
  ASSERT_TRUE(Storage.openFileForRead("TEST", "/bookmarks", restored));
  std::vector<uint8_t> restoredBytes(restored.fileSize());
  ASSERT_EQ(restored.read(restoredBytes.data(), restoredBytes.size()), static_cast<int>(restoredBytes.size()));
  EXPECT_EQ(restoredBytes, original);
  EXPECT_TRUE(restored.close());
  EXPECT_EQ(legacybookmarkedit::restoreOriginal("/bookmarks", book), legacybookmarkedit::Result::Conflict);
}

TEST(CrossInkBookmarkMove, EditRefusesChangedSourceAndExistingRecoveryFiles) {
  const std::string book = "/Authors/Writer/Book.epub";
  Storage.reset();
  const auto original = fixture(4);
  store(original);
  ASSERT_EQ(legacybookmarkedit::stage("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Ready);
  auto changed = original;
  changed.back() ^= 1;
  store(changed);
  EXPECT_EQ(legacybookmarkedit::publish("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Invalid);
  EXPECT_TRUE(Storage.exists("/bookmarks"));
  EXPECT_TRUE(Storage.exists("/bookmarks.bookmark-edit.tmp"));
  EXPECT_FALSE(Storage.exists("/bookmarks.bookmark-edit.bak"));
  EXPECT_EQ(legacybookmarkedit::stage("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Conflict);
}

TEST(CrossInkBookmarkMove, RestoreRenameFailurePreservesBackupAndStage) {
  const std::string book = "/Authors/Writer/Book.epub";
  Storage.reset();
  store(fixture(4));
  ASSERT_EQ(legacybookmarkedit::stage("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Ready);
  Storage.failNextRenameFrom("/bookmarks.bookmark-edit.tmp");
  ASSERT_EQ(legacybookmarkedit::publish("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Uncertain);
  Storage.failNextRenameFrom("/bookmarks.bookmark-edit.bak");
  EXPECT_EQ(legacybookmarkedit::restoreOriginal("/bookmarks", book), legacybookmarkedit::Result::Uncertain);
  EXPECT_FALSE(Storage.exists("/bookmarks"));
  EXPECT_TRUE(Storage.exists("/bookmarks.bookmark-edit.bak"));
  EXPECT_TRUE(Storage.exists("/bookmarks.bookmark-edit.tmp"));
  EXPECT_EQ(legacybookmarkedit::restoreOriginal("/bookmarks", book), legacybookmarkedit::Result::Restored);
}

TEST(CrossInkBookmarkMove, GuardFailureDoesNotRenameOriginal) {
  const std::string book = "/Authors/Writer/Book.epub";
  Storage.reset();
  const auto original = fixture(5);
  store(original);
  ASSERT_EQ(legacybookmarkedit::stage("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Ready);
  Storage.failNextWriteFor("/bookmarks.bookmark-edit.guard", 0);
  EXPECT_EQ(legacybookmarkedit::publish("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Uncertain);
  EXPECT_FALSE(Storage.exists("/bookmarks.bookmark-edit.bak"));
  EXPECT_TRUE(Storage.exists("/bookmarks.bookmark-edit.tmp"));
  HalFile primary;
  ASSERT_TRUE(Storage.openFileForRead("TEST", "/bookmarks", primary));
  std::vector<uint8_t> bytes(primary.fileSize());
  ASSERT_EQ(primary.read(bytes.data(), bytes.size()), static_cast<int>(bytes.size()));
  EXPECT_EQ(bytes, original);
  EXPECT_TRUE(primary.close());
}

TEST(CrossInkBookmarkMove, VerifiedArchivesPermitRepeatedEdits) {
  const std::string book = "/Authors/Writer/Book.epub";
  Storage.reset();
  auto original = fixture(5);
  original[1] = 2;
  original.insert(original.end() - 1, 124, 0x43);
  store(original);
  for (int slot = 0; slot < 2; ++slot) {
    ASSERT_EQ(legacybookmarkedit::stage("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Ready);
    ASSERT_EQ(legacybookmarkedit::publish("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Published);
    ASSERT_EQ(legacybookmarkedit::archivePublished("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Archived);
    EXPECT_TRUE(Storage.exists(("/bookmarks.bookmark-edit.bak-" + std::to_string(slot)).c_str()));
    EXPECT_TRUE(Storage.exists(("/bookmarks.bookmark-edit.guard-" + std::to_string(slot)).c_str()));
    EXPECT_FALSE(Storage.exists("/bookmarks.bookmark-edit.bak"));
    EXPECT_FALSE(Storage.exists("/bookmarks.bookmark-edit.guard"));
  }
}

TEST(CrossInkBookmarkMove, ArchiveRefusesWrongEditAndPreservesRecoveryFiles) {
  const std::string book = "/Authors/Writer/Book.epub";
  Storage.reset();
  store(fixture(5));
  ASSERT_EQ(legacybookmarkedit::stage("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Ready);
  ASSERT_EQ(legacybookmarkedit::publish("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Published);
  EXPECT_EQ(legacybookmarkedit::archivePublished("/bookmarks", "/wrong.epub", 0, 1),
            legacybookmarkedit::Result::Conflict);
  EXPECT_TRUE(Storage.exists("/bookmarks.bookmark-edit.bak"));
  EXPECT_TRUE(Storage.exists("/bookmarks.bookmark-edit.guard"));
  Storage.failNextRenameFrom("/bookmarks.bookmark-edit.bak");
  EXPECT_EQ(legacybookmarkedit::archivePublished("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Uncertain);
  EXPECT_TRUE(Storage.exists("/bookmarks.bookmark-edit.bak"));
  EXPECT_TRUE(Storage.exists("/bookmarks.bookmark-edit.guard"));
}

TEST(CrossInkBookmarkMove, InterruptedArchiveKeepsGuardAndArchivedBackup) {
  const std::string book = "/Authors/Writer/Book.epub";
  Storage.reset();
  store(fixture(5));
  ASSERT_EQ(legacybookmarkedit::stage("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Ready);
  ASSERT_EQ(legacybookmarkedit::publish("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Published);
  Storage.failNextRenameFrom("/bookmarks.bookmark-edit.guard");
  EXPECT_EQ(legacybookmarkedit::archivePublished("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Uncertain);
  EXPECT_TRUE(Storage.exists("/bookmarks"));
  EXPECT_TRUE(Storage.exists("/bookmarks.bookmark-edit.bak-0"));
  EXPECT_TRUE(Storage.exists("/bookmarks.bookmark-edit.guard"));
  EXPECT_EQ(legacybookmarkedit::stage("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Conflict);
  EXPECT_EQ(legacybookmarkedit::archivePublished("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Archived);
  EXPECT_TRUE(Storage.exists("/bookmarks.bookmark-edit.guard-0"));
  EXPECT_FALSE(Storage.exists("/bookmarks.bookmark-edit.guard"));
}

TEST(CrossInkBookmarkMove, InterruptedArchiveRefusesAmbiguousHistory) {
  const std::string book = "/Authors/Writer/Book.epub";
  Storage.reset();
  const auto original = fixture(5);
  store(original);
  ASSERT_EQ(legacybookmarkedit::stage("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Ready);
  ASSERT_EQ(legacybookmarkedit::publish("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Published);
  Storage.failNextRenameFrom("/bookmarks.bookmark-edit.guard");
  ASSERT_EQ(legacybookmarkedit::archivePublished("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Uncertain);
  HalFile extra;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/bookmarks.bookmark-edit.bak-1", extra));
  ASSERT_EQ(extra.write(original.data(), original.size()), original.size());
  ASSERT_TRUE(extra.close());
  EXPECT_EQ(legacybookmarkedit::archivePublished("/bookmarks", book, 0, 1), legacybookmarkedit::Result::Conflict);
  EXPECT_TRUE(Storage.exists("/bookmarks.bookmark-edit.guard"));
  EXPECT_FALSE(Storage.exists("/bookmarks.bookmark-edit.guard-0"));
  EXPECT_FALSE(Storage.exists("/bookmarks.bookmark-edit.guard-1"));
}
