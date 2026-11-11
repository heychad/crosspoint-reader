#include <CompletionMoveFile.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <string>

namespace cm = readingstats::completionmove;
namespace {
void put(const char* path, const std::string& data) {
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", path, file));
  ASSERT_EQ(file.write(reinterpret_cast<const uint8_t*>(data.data()), data.size()), data.size());
  ASSERT_TRUE(file.close());
}
class CompletionMoveFile : public testing::Test {
 protected:
  void SetUp() override { Storage.reset(); }
};
}  // namespace

TEST_F(CompletionMoveFile, MovesRecordedFileAndRetryRecognizesIt) {
  put("/from.epub", std::string(700, 'a'));
  cm::FileIdentity identity;
  ASSERT_EQ(cm::captureFile("/from.epub", identity), cm::FileResult::Ready);
  EXPECT_EQ(identity.size, 700u);
  EXPECT_EQ(cm::moveFileChecked("/from.epub", "/read/to.epub", identity), cm::FileResult::Moved);
  EXPECT_EQ(cm::moveFileChecked("/from.epub", "/read/to.epub", identity), cm::FileResult::AlreadyMoved);
  EXPECT_FALSE(Storage.exists("/from.epub"));
  EXPECT_TRUE(Storage.exists("/read/to.epub"));
}

TEST_F(CompletionMoveFile, NeverOverwritesOrMovesChangedInput) {
  put("/source", "original");
  cm::FileIdentity identity;
  ASSERT_EQ(cm::captureFile("/source", identity), cm::FileResult::Ready);
  put("/destination", "somebody else's book");
  EXPECT_EQ(cm::moveFileChecked("/source", "/destination", identity), cm::FileResult::Conflict);
  EXPECT_TRUE(Storage.remove("/destination"));
  put("/source", "different");
  EXPECT_EQ(cm::moveFileChecked("/source", "/destination", identity), cm::FileResult::Conflict);
  EXPECT_TRUE(Storage.exists("/source"));
  EXPECT_FALSE(Storage.exists("/destination"));
}

TEST_F(CompletionMoveFile, AbsentSidecarMustStayAbsent) {
  cm::FileIdentity identity;
  ASSERT_EQ(cm::captureFile("/source.key", identity), cm::FileResult::Absent);
  EXPECT_EQ(cm::moveFileChecked("/source.key", "/dest.key", identity), cm::FileResult::Absent);
  put("/dest.key", "unexpected");
  EXPECT_EQ(cm::moveFileChecked("/source.key", "/dest.key", identity), cm::FileResult::Conflict);
}

TEST_F(CompletionMoveFile, UncertainRenameIsRetriedWithoutOverwriting) {
  put("/source", "book");
  cm::FileIdentity identity;
  ASSERT_EQ(cm::captureFile("/source", identity), cm::FileResult::Ready);
  Storage.failNextRenameFrom("/source");
  EXPECT_EQ(cm::moveFileChecked("/source", "/destination", identity), cm::FileResult::Uncertain);
  EXPECT_EQ(cm::moveFileChecked("/source", "/destination", identity), cm::FileResult::Moved);
  EXPECT_EQ(cm::moveFileChecked("/source", "/destination", identity), cm::FileResult::AlreadyMoved);
}

TEST_F(CompletionMoveFile, DistinguishesDuplicateFromAChangedDestinationWithoutRemovingEither) {
  put("/source", "book");
  cm::FileIdentity identity;
  ASSERT_EQ(cm::captureFile("/source", identity), cm::FileResult::Ready);
  EXPECT_EQ(cm::inspectMoveFile("/source", "/destination", identity), cm::MoveFileState::AtSource);
  put("/destination", "book");
  EXPECT_EQ(cm::inspectMoveFile("/source", "/destination", identity), cm::MoveFileState::Duplicate);
  EXPECT_TRUE(Storage.exists("/source"));
  EXPECT_TRUE(Storage.exists("/destination"));
  put("/destination", "other");
  EXPECT_EQ(cm::inspectMoveFile("/source", "/destination", identity), cm::MoveFileState::Conflict);
  EXPECT_TRUE(Storage.exists("/source"));
  EXPECT_TRUE(Storage.exists("/destination"));
}

TEST_F(CompletionMoveFile, CopiesStateAndResumesCompleteStagingFile) {
  put("/source", std::string(700, 'p'));
  cm::FileIdentity identity;
  ASSERT_EQ(cm::captureFile("/source", identity), cm::FileResult::Ready);
  Storage.failNextRenameFrom("/destination.completion-copy");
  EXPECT_EQ(cm::copyFileChecked("/source", "/destination", identity), cm::FileResult::Uncertain);
  EXPECT_TRUE(Storage.exists("/source"));
  EXPECT_TRUE(Storage.exists("/destination.completion-copy"));
  EXPECT_FALSE(Storage.exists("/destination"));
  EXPECT_EQ(cm::copyFileChecked("/source", "/destination", identity), cm::FileResult::Copied);
  EXPECT_EQ(cm::copyFileChecked("/source", "/destination", identity), cm::FileResult::AlreadyCopied);
  EXPECT_TRUE(Storage.exists("/source"));
}

TEST_F(CompletionMoveFile, ResumesIncompleteStagingFileAndPreservesOriginal) {
  put("/source", "important progress");
  cm::FileIdentity identity;
  ASSERT_EQ(cm::captureFile("/source", identity), cm::FileResult::Ready);
  Storage.failNextWriteFor("/destination.completion-copy", 4);
  EXPECT_EQ(cm::copyFileChecked("/source", "/destination", identity), cm::FileResult::Uncertain);
  Storage.allowWritesFor("/destination.completion-copy");
  EXPECT_EQ(cm::copyFileChecked("/source", "/destination", identity), cm::FileResult::Copied);
  EXPECT_TRUE(Storage.exists("/source"));
  EXPECT_FALSE(Storage.exists("/destination.completion-copy"));
  EXPECT_TRUE(Storage.exists("/destination"));
}

TEST_F(CompletionMoveFile, ResumesLargeStateAfterInterruptedCopy) {
  put("/source", std::string(100 * 1024, 'p'));
  cm::FileIdentity identity;
  ASSERT_EQ(cm::captureFile("/source", identity), cm::FileResult::Ready);
  Storage.failNextWriteFor("/destination.completion-copy", 40 * 1024);
  EXPECT_EQ(cm::copyFileChecked("/source", "/destination", identity), cm::FileResult::Uncertain);
  Storage.allowWritesFor("/destination.completion-copy");
  EXPECT_EQ(cm::copyFileChecked("/source", "/destination", identity), cm::FileResult::Copied);
  EXPECT_TRUE(Storage.exists("/source"));
  EXPECT_TRUE(Storage.exists("/destination"));
}

TEST_F(CompletionMoveFile, RejectsAStagingFileThatIsNotTheSourcePrefix) {
  put("/source", "important progress");
  put("/destination.completion-copy", "wrong");
  cm::FileIdentity identity;
  ASSERT_EQ(cm::captureFile("/source", identity), cm::FileResult::Ready);
  EXPECT_EQ(cm::copyFileChecked("/source", "/destination", identity), cm::FileResult::Conflict);
  EXPECT_TRUE(Storage.exists("/destination.completion-copy"));
  EXPECT_FALSE(Storage.exists("/destination"));
}

TEST_F(CompletionMoveFile, RefusesToResumeStagingThatAliasesSource) {
  put("/source", "important progress");
  cm::FileIdentity identity;
  ASSERT_EQ(cm::captureFile("/source", identity), cm::FileResult::Ready);
  Storage.failNextAliasRenameFrom("/source");
  ASSERT_FALSE(Storage.rename("/source", "/destination.completion-copy"));
  EXPECT_EQ(cm::copyFileChecked("/source", "/destination", identity), cm::FileResult::Conflict);
  cm::FileIdentity after;
  ASSERT_EQ(cm::captureFile("/source", after), cm::FileResult::Ready);
  EXPECT_EQ(after.size, identity.size);
  EXPECT_EQ(after.checksum, identity.checksum);
  EXPECT_FALSE(Storage.exists("/destination"));
}

TEST_F(CompletionMoveFile, RefusesAnEmptyStagingFileThatAliasesSource) {
  put("/source", "");
  cm::FileIdentity identity;
  ASSERT_EQ(cm::captureFile("/source", identity), cm::FileResult::Ready);
  Storage.failNextAliasRenameFrom("/source");
  ASSERT_FALSE(Storage.rename("/source", "/destination.completion-copy"));
  EXPECT_EQ(cm::copyFileChecked("/source", "/destination", identity), cm::FileResult::Conflict);
  EXPECT_TRUE(Storage.exists("/source"));
  EXPECT_FALSE(Storage.exists("/destination"));
}

TEST_F(CompletionMoveFile, RefusesAnExistingDestinationThatAliasesSource) {
  put("/source", "important progress");
  cm::FileIdentity identity;
  ASSERT_EQ(cm::captureFile("/source", identity), cm::FileResult::Ready);
  Storage.failNextAliasRenameFrom("/source");
  ASSERT_FALSE(Storage.rename("/source", "/destination"));
  EXPECT_EQ(cm::copyFileChecked("/source", "/destination", identity), cm::FileResult::Conflict);
  EXPECT_TRUE(Storage.exists("/source"));
  EXPECT_TRUE(Storage.exists("/destination"));
}

TEST_F(CompletionMoveFile, CopiesAndRetriesAnIndependentEmptyFile) {
  put("/source", "");
  cm::FileIdentity identity;
  ASSERT_EQ(cm::captureFile("/source", identity), cm::FileResult::Ready);
  EXPECT_EQ(cm::copyFileChecked("/source", "/destination", identity), cm::FileResult::Copied);
  EXPECT_EQ(cm::copyFileChecked("/source", "/destination", identity), cm::FileResult::AlreadyCopied);
  EXPECT_TRUE(Storage.exists("/source"));
  EXPECT_FALSE(Storage.exists("/destination.completion-copy"));
  EXPECT_TRUE(Storage.exists("/destination"));
}
