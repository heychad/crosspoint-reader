#include <BookReadingStats.h>
#include <CompletionJournalIO.h>
#include <CompletionMovePlanIO.h>
#include <CompletionTransaction.h>
#include <CredentialIntegrity.h>
#include <CrossInkBookmarkMove.h>
#include <GlobalReadingStats.h>
#include <HalStorage.h>
#include <ReadingStatsStore.h>
#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace cm = readingstats::completionmove;
namespace cj = readingstats::completionjournal;
namespace {
constexpr char BOOK[] = "/Authors/Writer/Example.epub";
constexpr char DEST[] = "/read/Example.epub";
constexpr char PLAN[] = "/.completion-journal/.cmove-00";
constexpr char SEAL[] = "/.completion-journal/.cmoveseal-00";
class CompletionMovePlanIO : public testing::Test {
 protected:
  void SetUp() override {
    Storage.reset();
    HalFile file;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", BOOK, file));
    ASSERT_TRUE(file.close());
    const auto cache = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(BOOK));
    BookReadingStats book;
    ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(cache), book));
    GlobalReadingStats global;
    ASSERT_TRUE(global.save());
    ASSERT_EQ(readingstats::setClosedBookCompletionStats(BOOK, true, {2026, 11, 10}, cj::MOVE_TO_READ),
              readingstats::ClosedCompletionResult::Applied);
  }
};
}  // namespace

TEST_F(CompletionMovePlanIO, PublishesAndRecoversSamePlanWithoutOverwriting) {
  HalFile key;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", std::string(BOOK) + ".key", key));
  const uint8_t secret[] = {1, 2, 3};
  ASSERT_EQ(key.write(secret, sizeof(secret)), sizeof(secret));
  ASSERT_TRUE(key.close());
  auto plan = std::make_unique<cm::Plan>();
  size_t size = 0;
  std::string_view from, to;
  EXPECT_EQ(cm::inspectPlan(*plan, size, from, to), cm::PlanResult::Missing);
  ASSERT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Ready);
  ASSERT_EQ(cm::inspectPlan(*plan, size, from, to), cm::PlanResult::Ready);
  EXPECT_EQ(from, BOOK);
  EXPECT_EQ(to, DEST);
  cj::Record record{};
  size_t recordSize = 0;
  unsigned slot = 0;
  ASSERT_EQ(cj::scanRecords(record, recordSize, slot), cj::ScanResult::Candidate);
  std::array<cm::FileIdentity, cm::FILE_COUNT> identities{};
  ASSERT_TRUE(cm::decode(plan->data(), size, cj::checksum(record.data(), recordSize), from, to, identities.data()));
  EXPECT_TRUE(identities[0].present);
  EXPECT_TRUE(identities[1].present);
  EXPECT_EQ(identities[1].size, 3u);
  EXPECT_FALSE(identities[2].present);
  EXPECT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Ready);
  EXPECT_EQ(cm::preparePlan(BOOK, "/read/other.epub"), cm::PlanResult::Conflict);
  EXPECT_TRUE(Storage.exists(PLAN));
  EXPECT_TRUE(Storage.exists(SEAL));
  ASSERT_TRUE(Storage.rename(BOOK, DEST));
  EXPECT_EQ(cm::inspectPlan(*plan, size, from, to), cm::PlanResult::Ready);
  EXPECT_EQ(from, BOOK);
  EXPECT_EQ(to, DEST);
}

TEST_F(CompletionMovePlanIO, ReadOnlyVerificationDetectsIncompleteAndChangedMoves) {
  EXPECT_EQ(cm::verifyPreparedFiles(), cm::VerifyFilesResult::NoPlan);
  std::string progressSource, progressTarget;
  ASSERT_TRUE(cm::itemPaths(BOOK, DEST, cm::MOVED_FILE_COUNT, progressSource, progressTarget));
  HalFile progress;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", progressSource, progress));
  const uint8_t content[] = {1, 2, 3};
  ASSERT_EQ(progress.write(content, sizeof(content)), sizeof(content));
  ASSERT_TRUE(progress.close());
  ASSERT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Ready);
  EXPECT_EQ(cm::verifyPreparedFiles(), cm::VerifyFilesResult::NotReady);
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_FALSE(Storage.exists(DEST));
  ASSERT_EQ(cm::movePreparedFiles(), cm::MoveFilesResult::Moved);
  EXPECT_EQ(cm::verifyPreparedFiles(), cm::VerifyFilesResult::ContentMatches);
  Storage.failNextReadFor(progressTarget);
  EXPECT_EQ(cm::verifyPreparedFiles(), cm::VerifyFilesResult::Unavailable);
  EXPECT_TRUE(Storage.exists(progressSource.c_str()));
  EXPECT_TRUE(Storage.exists(progressTarget.c_str()));
  HalFile unexpected;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", progressTarget + ".completion-copy", unexpected));
  ASSERT_TRUE(unexpected.close());
  EXPECT_EQ(cm::verifyPreparedFiles(), cm::VerifyFilesResult::NotReady);
  ASSERT_TRUE(Storage.remove((progressTarget + ".completion-copy").c_str()));
  ASSERT_TRUE(Storage.openFileForWrite("TEST", progressTarget, progress));
  const uint8_t changed[] = {1, 2, 4};
  ASSERT_EQ(progress.write(changed, sizeof(changed)), sizeof(changed));
  ASSERT_TRUE(progress.close());
  EXPECT_EQ(cm::verifyPreparedFiles(), cm::VerifyFilesResult::NotReady);
  EXPECT_TRUE(Storage.exists(progressSource.c_str()));
  EXPECT_TRUE(Storage.exists(progressTarget.c_str()));
}

TEST_F(CompletionMovePlanIO, RejectsWrongBookAndOccupiedDestination) {
  EXPECT_EQ(cm::preparePlan("/another.epub", DEST), cm::PlanResult::Invalid);
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", DEST, file));
  ASSERT_TRUE(file.close());
  EXPECT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Conflict);
  EXPECT_FALSE(Storage.exists(PLAN));
}

TEST_F(CompletionMovePlanIO, PartialPlanAndSealNeverAuthorizeMove) {
  Storage.failNextWriteFor(PLAN, 4);
  EXPECT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Uncertain);
  auto plan = std::make_unique<cm::Plan>();
  size_t size = 0;
  std::string_view from, to;
  EXPECT_EQ(cm::inspectPlan(*plan, size, from, to), cm::PlanResult::Damaged);
  EXPECT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Damaged);
  EXPECT_FALSE(Storage.exists(SEAL));
}

TEST_F(CompletionMovePlanIO, IncompleteSealIsPreservedAndCannotAuthorizeMove) {
  Storage.failNextWriteFor(SEAL, 5);
  EXPECT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Uncertain);
  auto plan = std::make_unique<cm::Plan>();
  size_t size = 0;
  std::string_view from, to;
  EXPECT_EQ(cm::inspectPlan(*plan, size, from, to), cm::PlanResult::Damaged);
  EXPECT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Damaged);
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_FALSE(Storage.exists(DEST));
}

TEST_F(CompletionMovePlanIO, OrphanedSealIsNotMistakenForMissingPlan) {
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", SEAL, file));
  ASSERT_TRUE(file.close());
  auto plan = std::make_unique<cm::Plan>();
  size_t size = 0;
  std::string_view from, to;
  EXPECT_EQ(cm::inspectPlan(*plan, size, from, to), cm::PlanResult::Damaged);
  EXPECT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Conflict);
  EXPECT_FALSE(Storage.exists(PLAN));
}

TEST_F(CompletionMovePlanIO, ResumesFileSetAfterSidecarRenameFailure) {
  const std::string keyPath = std::string(BOOK) + ".key";
  std::string clippingSource, clippingTarget;
  ASSERT_TRUE(cm::itemPaths(BOOK, DEST, 4, clippingSource, clippingTarget));
  HalFile clip;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", clippingSource, clip));
  const uint8_t annotation[] = {9, 7, 5};
  ASSERT_EQ(clip.write(annotation, sizeof(annotation)), sizeof(annotation));
  ASSERT_TRUE(clip.close());
  std::string bookmarkSource, bookmarkTarget;
  ASSERT_TRUE(cm::itemPaths(BOOK, DEST, 8, bookmarkSource, bookmarkTarget));
  HalFile bookmark;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", bookmarkSource, bookmark));
  ASSERT_EQ(bookmark.write(annotation, sizeof(annotation)), sizeof(annotation));
  ASSERT_TRUE(bookmark.close());
  std::string progressSource, progressTarget, prefsSource, prefsTarget, statsSource, statsTarget;
  ASSERT_TRUE(cm::itemPaths(BOOK, DEST, 11, progressSource, progressTarget));
  ASSERT_TRUE(cm::itemPaths(BOOK, DEST, 14, prefsSource, prefsTarget));
  ASSERT_TRUE(cm::itemPaths(BOOK, DEST, 21, statsSource, statsTarget));
  HalFile progress;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", progressSource, progress));
  ASSERT_EQ(progress.write(annotation, sizeof(annotation)), sizeof(annotation));
  ASSERT_TRUE(progress.close());
  HalFile prefs;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", prefsSource, prefs));
  ASSERT_EQ(prefs.write(annotation, sizeof(annotation)), sizeof(annotation));
  ASSERT_TRUE(prefs.close());
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", keyPath, file));
  const uint8_t key[] = {1, 2, 3};
  ASSERT_EQ(file.write(key, sizeof(key)), sizeof(key));
  ASSERT_TRUE(file.close());
  ASSERT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Ready);
  Storage.failNextRenameFrom(keyPath);
  EXPECT_EQ(cm::movePreparedFiles(), cm::MoveFilesResult::RecoveryRequired);
  EXPECT_FALSE(Storage.exists(BOOK));
  EXPECT_TRUE(Storage.exists(DEST));
  EXPECT_TRUE(Storage.exists(keyPath.c_str()));
  EXPECT_EQ(cm::movePreparedFiles(), cm::MoveFilesResult::Moved);
  EXPECT_FALSE(Storage.exists(keyPath.c_str()));
  EXPECT_TRUE(Storage.exists((std::string(DEST) + ".key").c_str()));
  EXPECT_FALSE(Storage.exists(clippingSource.c_str()));
  EXPECT_TRUE(Storage.exists(clippingTarget.c_str()));
  EXPECT_FALSE(Storage.exists(bookmarkSource.c_str()));
  EXPECT_TRUE(Storage.exists(bookmarkTarget.c_str()));
  EXPECT_TRUE(Storage.exists(progressSource.c_str()));
  EXPECT_TRUE(Storage.exists(progressTarget.c_str()));
  EXPECT_TRUE(Storage.exists(prefsSource.c_str()));
  EXPECT_TRUE(Storage.exists(prefsTarget.c_str()));
  EXPECT_TRUE(Storage.exists(statsSource.c_str()));
  EXPECT_TRUE(Storage.exists(statsTarget.c_str()));
  EXPECT_EQ(cm::movePreparedFiles(), cm::MoveFilesResult::Moved);
  bool target = false;
  EXPECT_EQ(readingstats::probeClosedBookCompletion(BOOK, target), readingstats::CompletionProbeResult::Matching);
  EXPECT_TRUE(target);
  target = false;
  EXPECT_EQ(readingstats::probeClosedBookCompletion(DEST, target), readingstats::CompletionProbeResult::Matching);
  EXPECT_TRUE(target);
  EXPECT_EQ(readingstats::recoverClosedBookCompletionStats(DEST, true),
            readingstats::CompletionRecoveryResult::Recovered);
  EXPECT_EQ(readingstats::finalizeClosedBookCompletion(DEST, true),
            readingstats::CompletionFinalizeResult::ManualReview);
  target = false;
  EXPECT_EQ(readingstats::probeClosedBookCompletion(DEST, target), readingstats::CompletionProbeResult::Matching);
  EXPECT_TRUE(target);
  HalFile replacement;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", DEST, replacement));
  ASSERT_EQ(replacement.write(annotation, sizeof(annotation)), sizeof(annotation));
  ASSERT_TRUE(replacement.close());
  EXPECT_EQ(readingstats::probeClosedBookCompletion(DEST, target),
            readingstats::CompletionProbeResult::DifferentTransaction);
}

TEST_F(CompletionMovePlanIO, RefusesToStrandLegacyBookmarks) {
  const auto key = credential_integrity::crc32(BOOK);
  const auto path = "/.crosspoint/bookmarks/epub_" + std::to_string(key) + ".bin";
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", path, file));
  ASSERT_TRUE(file.close());
  EXPECT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Conflict);
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_TRUE(Storage.exists(path.c_str()));
  EXPECT_FALSE(Storage.exists(PLAN));
}

class CompletionMoveBookmarkArtifacts : public CompletionMovePlanIO,
                                        public testing::WithParamInterface<std::tuple<bool, bool, const char*>> {
 protected:
  std::string artifactPath() const {
    const auto [destination, crcKey, suffix] = GetParam();
    const std::string book = destination ? DEST : BOOK;
    const size_t key = crcKey ? credential_integrity::crc32(book) : std::hash<std::string>{}(book);
    return "/.crosspoint/bookmarks/epub_" + std::to_string(key) + ".bin" + suffix;
  }
};

TEST_P(CompletionMoveBookmarkArtifacts, PreservesRecoveryArtifactsAndRefusesBookMove) {
  const auto path = artifactPath();
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", path, file));
  const uint8_t recovery[] = {0x31, 0x42, 0x53};
  ASSERT_EQ(file.write(recovery, sizeof(recovery)), sizeof(recovery));
  ASSERT_TRUE(file.close());
  EXPECT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Conflict);
  EXPECT_EQ(cm::movePreparedFiles(), cm::MoveFilesResult::NoPlan);
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_FALSE(Storage.exists(DEST));
  EXPECT_FALSE(Storage.exists(PLAN));
  ASSERT_TRUE(Storage.openFileForRead("TEST", path, file));
  uint8_t preserved[sizeof(recovery)]{};
  ASSERT_EQ(file.read(preserved, sizeof(preserved)), sizeof(preserved));
  EXPECT_EQ(std::memcmp(preserved, recovery, sizeof(recovery)), 0);
  EXPECT_TRUE(file.close());
}

TEST_P(CompletionMoveBookmarkArtifacts, UnreadableRecoveryLookupCannotAuthorizeMove) {
  Storage.failReadsFor(artifactPath());
  EXPECT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Unavailable);
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_FALSE(Storage.exists(DEST));
  EXPECT_FALSE(Storage.exists(PLAN));
}

INSTANTIATE_TEST_SUITE_P(SourceAndDestination, CompletionMoveBookmarkArtifacts,
                         testing::Combine(testing::Bool(), testing::Bool(),
                                          testing::Values(".bookmark-edit.bak", ".bookmark-edit.guard")));

TEST_F(CompletionMovePlanIO, MigratesLegacyBookmarksAndResumesPartialRewrite) {
  std::string oldPath, newPath, secondOld, secondNew;
  ASSERT_TRUE(cm::itemPaths(BOOK, DEST, cm::STATE_FILE_END, oldPath, newPath));
  ASSERT_TRUE(cm::itemPaths(BOOK, DEST, cm::STATE_FILE_END + 1, secondOld, secondNew));
  if (oldPath == secondOld || newPath == secondNew || oldPath == secondNew || secondOld == newPath)
    GTEST_SKIP() << "legacy keys collide";
  std::vector<uint8_t> bytes{5, 1, 0};
  const auto addString = [&](const std::string& text) {
    const auto n = static_cast<uint32_t>(text.size());
    for (size_t i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(n >> (8 * i)));
    bytes.insert(bytes.end(), text.begin(), text.end());
  };
  addString("title");
  addString("author");
  addString(BOOK);
  bytes.resize(bytes.size() + 124, 0x55);
  HalFile old;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", oldPath, old));
  ASSERT_EQ(old.write(bytes.data(), bytes.size()), bytes.size());
  ASSERT_TRUE(old.close());
  HalFile other;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", secondOld, other));
  ASSERT_EQ(other.write(bytes.data(), bytes.size()), bytes.size());
  ASSERT_TRUE(other.close());
  ASSERT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Ready);
  Storage.failNextWriteFor(newPath + ".completion-copy", 7);
  EXPECT_EQ(cm::movePreparedFiles(), cm::MoveFilesResult::RecoveryRequired);
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_TRUE(Storage.exists(oldPath.c_str()));
  EXPECT_TRUE(Storage.exists(secondOld.c_str()));
  EXPECT_FALSE(Storage.exists(secondNew.c_str()));
  EXPECT_FALSE(Storage.exists(newPath.c_str()));
  Storage.allowWritesFor(newPath + ".completion-copy");
  EXPECT_EQ(cm::movePreparedFiles(), cm::MoveFilesResult::Moved);
  EXPECT_TRUE(Storage.exists(oldPath.c_str()));
  EXPECT_TRUE(Storage.exists(secondNew.c_str()));
  ASSERT_TRUE(Storage.openFileForRead("TEST", oldPath, old));
  HalFile migrated;
  ASSERT_TRUE(Storage.openFileForRead("TEST", newPath, migrated));
  bool complete = false;
  EXPECT_EQ(cm::compareBookmarkRewrite(old, migrated, BOOK, DEST, complete), cm::BookmarkRewriteResult::Ready);
  EXPECT_TRUE(complete);
  EXPECT_TRUE(old.close());
  EXPECT_TRUE(migrated.close());
  EXPECT_EQ(cm::movePreparedFiles(), cm::MoveFilesResult::Moved);
  EXPECT_EQ(cm::verifyPreparedFiles(), cm::VerifyFilesResult::ContentMatches);
}

TEST_F(CompletionMovePlanIO, RefusesPreexistingDestinationCache) {
  std::string oldPath, newPath;
  ASSERT_TRUE(cm::itemPaths(BOOK, DEST, 11, oldPath, newPath));
  const auto directory = newPath.substr(0, newPath.find_last_of('/'));
  ASSERT_TRUE(Storage.mkdir(directory.c_str()));
  EXPECT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Conflict);
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_FALSE(Storage.exists(PLAN));
}

TEST_F(CompletionMovePlanIO, CopiesLegacyCacheAndRestoresLegacyOnlyProgress) {
  const auto key = cj::checksum(reinterpret_cast<const uint8_t*>(BOOK), std::char_traits<char>::length(BOOK));
  if (key == std::hash<std::string>{}(BOOK)) GTEST_SKIP() << "legacy and native cache keys coincide";
  const auto legacy = "/.crosspoint/epub_" + std::to_string(key);
  ASSERT_TRUE(Storage.mkdir(legacy.c_str()));
  std::string legacySource, legacyTarget, currentSource, currentTarget;
  ASSERT_TRUE(cm::itemPaths(BOOK, DEST, cm::LEGACY_BOOKMARK_END, legacySource, legacyTarget));
  ASSERT_TRUE(cm::itemPaths(BOOK, DEST, cm::MOVED_FILE_COUNT, currentSource, currentTarget));
  const uint8_t progress[] = {1, 0, 17, 0};
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", legacySource, file));
  ASSERT_EQ(file.write(progress, sizeof(progress)), sizeof(progress));
  ASSERT_TRUE(file.close());
  std::string legacyPrefs, migratedPrefs, legacyMarker, migratedMarker;
  ASSERT_TRUE(cm::itemPaths(BOOK, DEST, cm::LEGACY_BOOKMARK_END + 3, legacyPrefs, migratedPrefs));
  ASSERT_TRUE(cm::itemPaths(BOOK, DEST, cm::LEGACY_BOOKMARK_END + 15, legacyMarker, migratedMarker));
  ASSERT_TRUE(Storage.openFileForWrite("TEST", legacyPrefs, file));
  ASSERT_EQ(file.write(progress, sizeof(progress)), sizeof(progress));
  ASSERT_TRUE(file.close());
  ASSERT_TRUE(Storage.openFileForWrite("TEST", legacyMarker, file));
  ASSERT_EQ(file.write(progress, 1), 1u);
  ASSERT_TRUE(file.close());
  ASSERT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Ready);
  EXPECT_EQ(cm::movePreparedFiles(), cm::MoveFilesResult::Moved);
  EXPECT_TRUE(Storage.exists(legacySource.c_str()));
  EXPECT_TRUE(Storage.exists(legacyTarget.c_str()));
  EXPECT_TRUE(Storage.exists(currentTarget.c_str()));
  EXPECT_TRUE(Storage.exists(migratedPrefs.c_str()));
  EXPECT_TRUE(Storage.exists(migratedMarker.c_str()));
  EXPECT_EQ(cm::movePreparedFiles(), cm::MoveFilesResult::Moved);
  EXPECT_EQ(cm::verifyPreparedFiles(), cm::VerifyFilesResult::ContentMatches);
  ASSERT_TRUE(Storage.openFileForRead("TEST", currentTarget, file));
  uint8_t copied[sizeof(progress)]{};
  ASSERT_EQ(file.read(copied, sizeof(copied)), sizeof(copied));
  EXPECT_EQ(std::memcmp(copied, progress, sizeof(progress)), 0);
  EXPECT_TRUE(file.close());
}

TEST_F(CompletionMovePlanIO, NativeProgressTakesPrecedenceOverLegacyProgress) {
  std::string legacySource, legacyTarget, nativeSource, nativeTarget;
  ASSERT_TRUE(cm::itemPaths(BOOK, DEST, cm::LEGACY_BOOKMARK_END, legacySource, legacyTarget));
  ASSERT_TRUE(cm::itemPaths(BOOK, DEST, cm::MOVED_FILE_COUNT, nativeSource, nativeTarget));
  if (legacySource == nativeSource) GTEST_SKIP() << "legacy and native cache keys coincide";
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", legacySource, file));
  const uint8_t legacy[] = {1, 0, 2, 0};
  ASSERT_EQ(file.write(legacy, sizeof(legacy)), sizeof(legacy));
  ASSERT_TRUE(file.close());
  ASSERT_TRUE(Storage.openFileForWrite("TEST", nativeSource, file));
  const uint8_t native[] = {4, 0, 7, 0};
  ASSERT_EQ(file.write(native, sizeof(native)), sizeof(native));
  ASSERT_TRUE(file.close());
  ASSERT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Ready);
  ASSERT_EQ(cm::movePreparedFiles(), cm::MoveFilesResult::Moved);
  ASSERT_TRUE(Storage.openFileForRead("TEST", nativeTarget, file));
  uint8_t result[sizeof(native)]{};
  ASSERT_EQ(file.read(result, sizeof(result)), sizeof(result));
  EXPECT_EQ(std::memcmp(result, native, sizeof(native)), 0);
  EXPECT_TRUE(file.close());
  EXPECT_TRUE(Storage.exists(legacyTarget.c_str()));
  EXPECT_EQ(cm::verifyPreparedFiles(), cm::VerifyFilesResult::ContentMatches);
}

TEST_F(CompletionMovePlanIO, PreservesNewDestinationCollision) {
  ASSERT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Ready);
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", DEST, file));
  ASSERT_TRUE(file.close());
  EXPECT_EQ(cm::movePreparedFiles(), cm::MoveFilesResult::Conflict);
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_TRUE(Storage.exists(DEST));
}

TEST_F(CompletionMovePlanIO, InterruptedProgressCopyLeavesBookAndOriginalStateInPlace) {
  std::string source, target;
  ASSERT_TRUE(cm::itemPaths(BOOK, DEST, 11, source, target));
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", source, file));
  const uint8_t progress[] = {1, 2, 3, 4, 5, 6};
  ASSERT_EQ(file.write(progress, sizeof(progress)), sizeof(progress));
  ASSERT_TRUE(file.close());
  ASSERT_EQ(cm::preparePlan(BOOK, DEST), cm::PlanResult::Ready);
  Storage.failNextWriteFor(target + ".completion-copy", 3);
  EXPECT_EQ(cm::movePreparedFiles(), cm::MoveFilesResult::RecoveryRequired);
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_FALSE(Storage.exists(DEST));
  EXPECT_TRUE(Storage.exists(source.c_str()));
  EXPECT_FALSE(Storage.exists(target.c_str()));
  EXPECT_TRUE(Storage.exists((target + ".completion-copy").c_str()));
  Storage.allowWritesFor(target + ".completion-copy");
  EXPECT_EQ(cm::movePreparedFiles(), cm::MoveFilesResult::Moved);
  EXPECT_TRUE(Storage.exists(source.c_str()));
  EXPECT_TRUE(Storage.exists(target.c_str()));
}
