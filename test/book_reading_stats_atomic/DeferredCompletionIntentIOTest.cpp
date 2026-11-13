#include <BookReadingStats.h>
#include <CompletionJournal.h>
#include <CompletionJournalIO.h>
#include <CompletionMovePlanIO.h>
#include <CompletionTransaction.h>
#include <DeferredCompletionIntent.h>
#include <DeferredCompletionIntentIO.h>
#include <DeferredCompletionMove.h>
#include <GlobalReadingStats.h>
#include <HalStorage.h>
#include <ReadingStatsStore.h>
#include <StatsSessionLease.h>
#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>

namespace dc = readingstats::deferredcompletion;
namespace cj = readingstats::completionjournal;
namespace {
constexpr char BOOK[] = "/Authors/Writer/Book.epub";
class DeferredCompletionIntentIO : public testing::Test {
 protected:
  void SetUp() override {
    Storage.reset();
    HalFile file;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", BOOK, file));
    ASSERT_TRUE(file.close());
    const auto cache = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(BOOK));
    ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(cache), BookReadingStats{}));
    ASSERT_TRUE(GlobalReadingStats{}.save());
  }
  void start() {
    ASSERT_EQ(readingstats::setClosedBookCompletionStats(BOOK, true, {2026, 11, 11}, cj::DEFER_MOVE_TO_READ),
              readingstats::ClosedCompletionResult::Applied);
  }
};
}  // namespace

TEST_F(DeferredCompletionIntentIO, PublishesAppliedIntentAndRetriesIdempotently) {
  start();
  ASSERT_EQ(dc::publish(BOOK), dc::PublishResult::Published);
  EXPECT_EQ(dc::publish(BOOK), dc::PublishResult::AlreadyPublished);
  EXPECT_TRUE(Storage.exists("/.completion-deferred/.defer-00"));
  EXPECT_TRUE(Storage.exists("/.completion-deferred/.dseal-00"));
  EXPECT_EQ(readingstats::finalizeClosedBookCompletion(BOOK, true), readingstats::CompletionFinalizeResult::Finalized);
  HalFile file;
  ASSERT_TRUE(Storage.openFileForRead("TEST", "/.completion-deferred/.defer-00", file));
  dc::Record bytes{};
  const size_t length = file.fileSize();
  ASSERT_EQ(file.read(bytes.data(), length), static_cast<int>(length));
  ASSERT_TRUE(file.close());
  uint64_t token = 0;
  std::string_view source;
  ASSERT_TRUE(dc::decodeWithHash(bytes.data(), length, token, source));
  EXPECT_EQ(source, BOOK);
}

TEST_F(DeferredCompletionIntentIO, FindsSealedIntentWithoutClaimingJournalRetirement) {
  dc::Record output{};
  output.fill(0xa5);
  size_t size = 77;
  unsigned slot = 99;
  EXPECT_EQ(dc::find(BOOK, output, size, slot), dc::FindResult::Missing);
  EXPECT_EQ(output[0], 0xa5);
  EXPECT_EQ(size, 77u);
  EXPECT_EQ(slot, 99u);
  start();
  ASSERT_EQ(dc::publish(BOOK), dc::PublishResult::Published);
  ASSERT_EQ(dc::find(BOOK, output, size, slot), dc::FindResult::Found);
  EXPECT_EQ(slot, 0u);
  uint64_t token = 0;
  std::string_view source;
  ASSERT_TRUE(dc::decodeWithHash(output.data(), size, token, source));
  EXPECT_EQ(source, BOOK);
  bool target = false;
  EXPECT_EQ(readingstats::probeClosedBookCompletion(BOOK, target), readingstats::CompletionProbeResult::Matching);
  EXPECT_TRUE(target);
  EXPECT_EQ(cj::deferredRetirementProof(BOOK, token), cj::DeferredRetirementProof::Busy);
  dc::Record ready{};
  ready.fill(0x5a);
  size_t readySize = 88;
  unsigned readySlot = 42;
  EXPECT_EQ(dc::findReady(BOOK, ready, readySize, readySlot), dc::FindResult::Busy);
  EXPECT_EQ(ready[0], 0x5a);
  EXPECT_EQ(readySize, 88u);
  EXPECT_EQ(readySlot, 42u);
  ASSERT_EQ(readingstats::finalizeClosedBookCompletion(BOOK, true), readingstats::CompletionFinalizeResult::Finalized);
  EXPECT_EQ(cj::deferredRetirementProof(BOOK, token), cj::DeferredRetirementProof::Retired);
  EXPECT_EQ(dc::findReady(BOOK, ready, readySize, readySlot), dc::FindResult::Found);
  EXPECT_EQ(readySlot, 0u);
  EXPECT_EQ(cj::deferredRetirementProof("/other.epub", token), cj::DeferredRetirementProof::Missing);
  EXPECT_EQ(cj::deferredRetirementProof(BOOK, token ^ 1), cj::DeferredRetirementProof::Missing);
  EXPECT_EQ(dc::find("/other.epub", output, size, slot), dc::FindResult::Missing);
}

TEST_F(DeferredCompletionIntentIO, FindRefusesARecordWithIncompleteSeal) {
  start();
  Storage.failNextWriteFor("/.completion-deferred/.dseal-00", 3);
  ASSERT_EQ(dc::publish(BOOK), dc::PublishResult::Unavailable);
  dc::Record output{};
  output.fill(0xa5);
  size_t size = 77;
  unsigned slot = 99;
  EXPECT_EQ(dc::find(BOOK, output, size, slot), dc::FindResult::Blocked);
  EXPECT_EQ(output[0], 0xa5);
  EXPECT_EQ(size, 77u);
  EXPECT_EQ(slot, 99u);
}

TEST_F(DeferredCompletionIntentIO, FindWaitsForReaderLease) {
  start();
  ASSERT_EQ(readingstats::finalizeClosedBookCompletion(BOOK, true), readingstats::CompletionFinalizeResult::Finalized);
  StatsSessionLease lease;
  ASSERT_TRUE(lease.acquire());
  dc::Record output{};
  size_t size = 0;
  unsigned slot = 99;
  EXPECT_EQ(dc::find(BOOK, output, size, slot), dc::FindResult::Busy);
  ASSERT_TRUE(lease.release());
  EXPECT_EQ(dc::find(BOOK, output, size, slot), dc::FindResult::Found);
}

TEST_F(DeferredCompletionIntentIO, DiscoversReadyIntentOnlyAfterJournalRetirement) {
  dc::Record output{};
  output.fill(0xa5);
  size_t size = 77;
  unsigned slot = 99;
  EXPECT_EQ(dc::discoverReady(output, size, slot), dc::FindResult::Missing);
  start();
  ASSERT_EQ(dc::publish(BOOK), dc::PublishResult::Published);
  EXPECT_EQ(dc::discoverReady(output, size, slot), dc::FindResult::Busy);
  EXPECT_EQ(output[0], 0xa5);
  EXPECT_EQ(size, 77u);
  EXPECT_EQ(slot, 99u);
  ASSERT_EQ(readingstats::finalizeClosedBookCompletion(BOOK, true), readingstats::CompletionFinalizeResult::Finalized);
  StatsSessionLease lease;
  ASSERT_TRUE(lease.acquire());
  EXPECT_EQ(dc::discoverReady(output, size, slot), dc::FindResult::Busy);
  ASSERT_TRUE(lease.release());
  ASSERT_EQ(dc::discoverReady(output, size, slot), dc::FindResult::Found);
  uint64_t hash = 0;
  std::string_view source;
  ASSERT_TRUE(dc::decodeWithHash(output.data(), size, hash, source));
  EXPECT_EQ(source, BOOK);
  EXPECT_EQ(slot, 0u);
}

TEST_F(DeferredCompletionIntentIO, DiscoveryRejectsDamagedOrUnsealedRecords) {
  start();
  Storage.failNextWriteFor("/.completion-deferred/.dseal-00", 3);
  ASSERT_EQ(dc::publish(BOOK), dc::PublishResult::Unavailable);
  dc::Record output{};
  output.fill(0xa5);
  size_t size = 77;
  unsigned slot = 99;
  EXPECT_EQ(dc::discoverReady(output, size, slot), dc::FindResult::Blocked);
  EXPECT_EQ(output[0], 0xa5);
  EXPECT_EQ(size, 77u);
  EXPECT_EQ(slot, 99u);
}

TEST_F(DeferredCompletionIntentIO, DiscoveryAuditsRetiredReceiptsBeforeReturningMissing) {
  start();
  ASSERT_EQ(readingstats::finalizeClosedBookCompletion(BOOK, true), readingstats::CompletionFinalizeResult::Finalized);
  ASSERT_EQ(readingstats::setClosedBookCompletionStats(BOOK, false, {2026, 11, 11}),
            readingstats::ClosedCompletionResult::Applied);
  ASSERT_EQ(readingstats::finalizeClosedBookCompletion(BOOK, false), readingstats::CompletionFinalizeResult::Finalized);
  ASSERT_EQ(dc::cancelIfUnfinished(BOOK), dc::CancelResult::Cancelled);
  dc::Record output{};
  size_t size = 0;
  unsigned slot = 99;
  EXPECT_EQ(dc::discoverReady(output, size, slot), dc::FindResult::Missing);
  HalFile receipt;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/.completion-deferred/.dretire-00", receipt));
  const uint8_t invalid = 0;
  ASSERT_EQ(receipt.write(&invalid, 1), 1u);
  ASSERT_TRUE(receipt.close());
  EXPECT_EQ(dc::discoverReady(output, size, slot), dc::FindResult::Blocked);
  EXPECT_TRUE(Storage.exists(BOOK));
}

TEST_F(DeferredCompletionIntentIO, DiscoverySkipsRetiredRequestsAndFindsNextBook) {
  start();
  ASSERT_EQ(readingstats::finalizeClosedBookCompletion(BOOK, true), readingstats::CompletionFinalizeResult::Finalized);
  ASSERT_EQ(readingstats::setClosedBookCompletionStats(BOOK, false, {2026, 11, 11}),
            readingstats::ClosedCompletionResult::Applied);
  ASSERT_EQ(readingstats::finalizeClosedBookCompletion(BOOK, false), readingstats::CompletionFinalizeResult::Finalized);
  ASSERT_EQ(dc::cancelIfUnfinished(BOOK), dc::CancelResult::Cancelled);
  const std::string other = "/Authors/Writer/Other.epub";
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", other, file));
  ASSERT_TRUE(file.close());
  const auto cache = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(other));
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(cache), BookReadingStats{}));
  ASSERT_EQ(readingstats::setClosedBookCompletionStats(other, true, {2026, 11, 11}, cj::DEFER_MOVE_TO_READ),
            readingstats::ClosedCompletionResult::Applied);
  ASSERT_EQ(readingstats::finalizeClosedBookCompletion(other, true), readingstats::CompletionFinalizeResult::Finalized);
  dc::Record output{};
  size_t size = 0;
  unsigned slot = 99;
  ASSERT_EQ(dc::discoverReady(output, size, slot), dc::FindResult::Found);
  uint64_t hash = 0;
  std::string_view source;
  ASSERT_TRUE(dc::decodeWithHash(output.data(), size, hash, source));
  EXPECT_EQ(source, other);
  EXPECT_EQ(slot, 1u);
}

TEST_F(DeferredCompletionIntentIO, ReadyRequiresIntactRetiredJournalEvidence) {
  start();
  ASSERT_EQ(readingstats::finalizeClosedBookCompletion(BOOK, true), readingstats::CompletionFinalizeResult::Finalized);
  ASSERT_TRUE(Storage.remove("/.completion-journal/.completion-00"));
  dc::Record output{};
  output.fill(0xa5);
  size_t size = 77;
  unsigned slot = 99;
  EXPECT_EQ(dc::findReady(BOOK, output, size, slot), dc::FindResult::Blocked);
  EXPECT_EQ(output[0], 0xa5);
  EXPECT_EQ(size, 77u);
  EXPECT_EQ(slot, 99u);
}

TEST_F(DeferredCompletionIntentIO, FindsRetiredEvidenceAfterJournalRollover) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  for (int i = 1; i <= 64; ++i) {
    const bool completed = (i % 2) == 0;
    ASSERT_EQ(setClosedBookCompletionStats(BOOK, completed, {2026, 11, 11}), ClosedCompletionResult::Applied) << i;
    ASSERT_EQ(finalizeClosedBookCompletion(BOOK, completed), CompletionFinalizeResult::Finalized) << i;
  }
  ASSERT_TRUE(Storage.exists("/.completion-history-000000/.archive-00"));
  dc::Record record{};
  size_t size = 0;
  unsigned slot = 99;
  EXPECT_EQ(dc::findReady(BOOK, record, size, slot), dc::FindResult::Found);
  EXPECT_EQ(slot, 0u);
  ASSERT_TRUE(Storage.removeDir("/.completion-journal"));
  EXPECT_EQ(dc::findReady(BOOK, record, size, slot), dc::FindResult::Found);
  Storage.failReadsFor("/.completion-history-000000/.archive-00");
  EXPECT_EQ(dc::findReady(BOOK, record, size, slot), dc::FindResult::Unavailable);
}

TEST_F(DeferredCompletionIntentIO, RefusesDamagedArchivedRetirementEvidence) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  for (int i = 1; i <= 64; ++i) {
    const bool completed = (i % 2) == 0;
    ASSERT_EQ(setClosedBookCompletionStats(BOOK, completed, {2026, 11, 11}), ClosedCompletionResult::Applied) << i;
    ASSERT_EQ(finalizeClosedBookCompletion(BOOK, completed), CompletionFinalizeResult::Finalized) << i;
  }
  dc::Record record{};
  size_t size = 0;
  unsigned slot = 99;
  HalFile corrupt;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/.completion-history-000000/.cseal-00", corrupt));
  const uint8_t invalid = 0;
  ASSERT_EQ(corrupt.write(&invalid, 1), 1u);
  ASSERT_TRUE(corrupt.close());
  EXPECT_EQ(dc::findReady(BOOK, record, size, slot), dc::FindResult::Blocked);
  ASSERT_TRUE(Storage.remove("/.completion-history-000000/.cretire-00"));
  EXPECT_EQ(dc::findReady(BOOK, record, size, slot), dc::FindResult::Blocked);
  EXPECT_TRUE(Storage.exists(BOOK));
}

TEST_F(DeferredCompletionIntentIO, FindsEvidenceInLaterArchive) {
  using namespace readingstats;
  for (int i = 0; i < 64; ++i) {
    const bool completed = (i % 2) == 0;
    ASSERT_EQ(setClosedBookCompletionStats(BOOK, completed, {2026, 11, 11}), ClosedCompletionResult::Applied) << i;
    ASSERT_EQ(finalizeClosedBookCompletion(BOOK, completed), CompletionFinalizeResult::Finalized) << i;
  }
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  for (int i = 1; i <= 64; ++i) {
    const bool completed = (i % 2) == 0;
    ASSERT_EQ(setClosedBookCompletionStats(BOOK, completed, {2026, 11, 11}), ClosedCompletionResult::Applied) << i;
    ASSERT_EQ(finalizeClosedBookCompletion(BOOK, completed), CompletionFinalizeResult::Finalized) << i;
  }
  ASSERT_TRUE(Storage.exists("/.completion-history-000001/.archive-00"));
  dc::Record record{};
  size_t size = 0;
  unsigned slot = 99;
  EXPECT_EQ(dc::findReady(BOOK, record, size, slot), dc::FindResult::Found);
}

TEST_F(DeferredCompletionIntentIO, AssessmentHonorsLaterUnfinishedStatus) {
  using namespace readingstats;
  EXPECT_EQ(dc::assess(BOOK), dc::MoveAssessment::Missing);
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  EXPECT_EQ(dc::assess(BOOK), dc::MoveAssessment::Ready);
  EXPECT_EQ(dc::cancelIfUnfinished(BOOK), dc::CancelResult::StillFinished);
  ASSERT_EQ(setClosedBookCompletionStats(BOOK, false, {2026, 11, 11}), ClosedCompletionResult::Applied);
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, false), CompletionFinalizeResult::Finalized);
  EXPECT_EQ(dc::assess(BOOK), dc::MoveAssessment::Unfinished);
  EXPECT_EQ(dc::cancelIfUnfinished(BOOK), dc::CancelResult::Cancelled);
  EXPECT_TRUE(Storage.exists("/.completion-deferred/.dretire-00"));
  EXPECT_EQ(dc::assess(BOOK), dc::MoveAssessment::Missing);
  EXPECT_EQ(dc::cancelIfUnfinished(BOOK), dc::CancelResult::Missing);
  EXPECT_TRUE(Storage.exists(BOOK));
}

TEST_F(DeferredCompletionIntentIO, DamagedCancellationReceiptKeepsBookAndBlocksRetry) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  ASSERT_EQ(setClosedBookCompletionStats(BOOK, false, {2026, 11, 11}), ClosedCompletionResult::Applied);
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, false), CompletionFinalizeResult::Finalized);
  Storage.failNextWriteFor("/.completion-deferred/.dretire-00", 3);
  EXPECT_EQ(dc::cancelIfUnfinished(BOOK), dc::CancelResult::Unavailable);
  EXPECT_EQ(dc::cancelIfUnfinished(BOOK), dc::CancelResult::Blocked);
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_FALSE(
      BookReadingStats::load(bookDirectory("/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(BOOK))))
          .isCompleted);
}

TEST_F(DeferredCompletionIntentIO, AssessmentBlocksMissingSourceAndPendingStatsTemp) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  const auto cache = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(BOOK));
  const auto temporary = bookDirectory(cache) + "/stats_v5.bin.tmp";
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", temporary.c_str(), file));
  ASSERT_TRUE(file.close());
  EXPECT_EQ(dc::assess(BOOK), dc::MoveAssessment::Blocked);
  ASSERT_TRUE(Storage.remove(temporary.c_str()));
  ASSERT_TRUE(Storage.remove(BOOK));
  EXPECT_EQ(dc::assess(BOOK), dc::MoveAssessment::Blocked);
}

TEST_F(DeferredCompletionIntentIO, PreparesClosedBookMoveWithoutMovingFiles) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  ASSERT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Prepared);
  EXPECT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Prepared);
  EXPECT_EQ(dc::prepareMove(BOOK, "/read/Different.epub", {2026, 11, 11}), dc::PrepareMoveResult::RecoveryRequired);
  completionmove::Plan plan{};
  size_t planSize = 0;
  std::string_view source, destination;
  ASSERT_EQ(completionmove::inspectPlan(plan, planSize, source, destination), completionmove::PlanResult::Ready);
  EXPECT_EQ(source, BOOK);
  EXPECT_EQ(destination, "/read/Book.epub");
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_FALSE(Storage.exists("/read/Book.epub"));
  bool completed = false;
  uint8_t policy = 0;
  ASSERT_EQ(probeClosedBookCompletion(BOOK, completed, &policy), CompletionProbeResult::Matching);
  EXPECT_TRUE(completed);
  EXPECT_EQ(policy, cj::MOVE_TO_READ | cj::MOVE_FROM_DEFERRED);
  EXPECT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::ManualReview);
}

TEST_F(DeferredCompletionIntentIO, RecoversMatchingRetiredRequestForPendingMove) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  dc::Record intent{};
  size_t intentSize = 0;
  unsigned slot = 99;
  ASSERT_EQ(dc::findReady(BOOK, intent, intentSize, slot), dc::FindResult::Found);
  ASSERT_EQ(dc::findForMoveRecovery(BOOK, intent, intentSize, slot), dc::FindResult::Missing);
  ASSERT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Prepared);
  dc::Seal receipt{};
  ASSERT_TRUE(dc::encodeSeal(slot, intent, intentSize, receipt, true));
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/.completion-deferred/.dretire-00", file));
  ASSERT_EQ(file.write(receipt.data(), receipt.size()), receipt.size());
  ASSERT_TRUE(file.close());
  dc::Record recovered{};
  size_t recoveredSize = 0;
  unsigned recoveredSlot = 99;
  EXPECT_EQ(dc::find(BOOK, recovered, recoveredSize, recoveredSlot), dc::FindResult::Missing);
  EXPECT_EQ(dc::findForMoveRecovery(BOOK, recovered, recoveredSize, recoveredSlot), dc::FindResult::Found);
  EXPECT_EQ(recoveredSlot, slot);
  EXPECT_EQ(recoveredSize, intentSize);
  EXPECT_EQ(dc::findForMoveRecovery("/Authors/Writer/Other.epub", recovered, recoveredSize, recoveredSlot),
            dc::FindResult::Conflict);
  StatsSessionLease lease;
  EXPECT_FALSE(lease.acquire());
  EXPECT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Prepared);
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/.completion-deferred/.dretire-00", file));
  const uint8_t bad = 1;
  ASSERT_EQ(file.write(&bad, 1), 1u);
  ASSERT_TRUE(file.close());
  EXPECT_EQ(dc::findForMoveRecovery(BOOK, recovered, recoveredSize, recoveredSlot), dc::FindResult::Blocked);
  EXPECT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::RecoveryRequired);
  EXPECT_TRUE(Storage.exists(BOOK));
}

TEST_F(DeferredCompletionIntentIO, PreparedMoveCarriesBookAndCompletionState) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  ASSERT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Prepared);
  EXPECT_EQ(dc::retireMovedIntent(BOOK), dc::RetireMoveResult::RecoveryRequired);
  EXPECT_FALSE(Storage.exists("/.completion-deferred/.dretire-00"));
  ASSERT_EQ(completionmove::movePreparedFiles(), completionmove::MoveFilesResult::Moved);
  EXPECT_TRUE(Storage.exists("/.crosspoint/library.dirty"));
  EXPECT_EQ(completionmove::verifyPreparedFiles(), completionmove::VerifyFilesResult::ContentMatches);
  EXPECT_FALSE(Storage.exists(BOOK));
  EXPECT_TRUE(Storage.exists("/read/Book.epub"));
  bool completed = false;
  EXPECT_EQ(probeClosedBookCompletion("/read/Book.epub", completed), CompletionProbeResult::Matching);
  EXPECT_TRUE(completed);
  EXPECT_EQ(dc::retireMovedIntent(BOOK), dc::RetireMoveResult::Retired);
  EXPECT_EQ(dc::retireMovedIntent(BOOK), dc::RetireMoveResult::AlreadyRetired);
  EXPECT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Prepared);
  EXPECT_EQ(finalizeClosedBookCompletion("/read/Book.epub", true), CompletionFinalizeResult::ManualReview);
}

TEST_F(DeferredCompletionIntentIO, DiscoveryFindsInterruptedMoveEvenAfterSourceAndIntentMove) {
  using namespace readingstats;
  dc::Record record{};
  record.fill(0xa5);
  size_t size = 77;
  unsigned slot = 99;
  EXPECT_EQ(dc::discoverResumable(record, size, slot), dc::FindResult::Missing);
  start();
  EXPECT_EQ(dc::discoverResumable(record, size, slot), dc::FindResult::Busy);
  EXPECT_EQ(record[0], 0xa5);
  EXPECT_EQ(size, 77u);
  EXPECT_EQ(slot, 99u);
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  ASSERT_EQ(dc::discoverResumable(record, size, slot), dc::FindResult::Found);
  ASSERT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Prepared);
  ASSERT_EQ(dc::discoverResumable(record, size, slot), dc::FindResult::Found);
  ASSERT_EQ(completionmove::movePreparedFiles(), completionmove::MoveFilesResult::Moved);
  ASSERT_EQ(dc::retireMovedIntent(BOOK), dc::RetireMoveResult::Retired);
  ASSERT_FALSE(Storage.exists(BOOK));
  ASSERT_EQ(dc::discoverResumable(record, size, slot), dc::FindResult::Found);
  uint64_t hash = 0;
  std::string_view source;
  ASSERT_TRUE(dc::decodeWithHash(record.data(), size, hash, source));
  EXPECT_EQ(source, BOOK);
  EXPECT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Prepared);
  ASSERT_TRUE(Storage.remove("/.completion-deferred/.defer-00"));
  EXPECT_EQ(dc::discoverResumable(record, size, slot), dc::FindResult::Blocked);
  EXPECT_TRUE(Storage.exists("/read/Book.epub"));
}

TEST_F(DeferredCompletionIntentIO, IdentifiesExactRetiredMoveJournal) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  dc::Record intent{};
  size_t intentSize = 0;
  unsigned intentSlot = 0;
  ASSERT_EQ(dc::findReady(BOOK, intent, intentSize, intentSlot), dc::FindResult::Found);
  uint64_t origin = 0;
  std::string_view source;
  ASSERT_TRUE(dc::decodeWithHash(intent.data(), intentSize, origin, source));
  ASSERT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Prepared);
  cj::Record move{};
  size_t moveSize = 0;
  unsigned moveSlot = 0;
  ASSERT_EQ(cj::scanRecords(move, moveSize, moveSlot), cj::ScanResult::Candidate);
  const auto moveHash = cj::checksum(move.data(), moveSize);
  EXPECT_EQ(cj::retiredDeferredMoveProof(BOOK, origin, moveHash), cj::DeferredRetirementProof::Busy);
  // This test exercises journal recognition only; it does not authorize finalization.
  ASSERT_EQ(cj::retire(), cj::RetirementResult::Retired);
  EXPECT_EQ(cj::retiredDeferredMoveProof(BOOK, origin, moveHash), cj::DeferredRetirementProof::Retired);
  EXPECT_EQ(completionmove::inspectRetiredPlan(BOOK, "/read/Book.epub", origin, moveHash),
            completionmove::RetiredPlanResult::Ready);
  EXPECT_EQ(completionmove::inspectRetiredPlan(BOOK, "/read/Other.epub", origin, moveHash),
            completionmove::RetiredPlanResult::Conflict);
  ASSERT_EQ(cj::createRecord(2, move, moveSize), cj::CreateResult::Created);
  ASSERT_EQ(cj::createSeal(2, move, moveSize), cj::CreateResult::Created);
  cj::Seal duplicate{};
  ASSERT_TRUE(cj::encodeRetirement(2, move, moveSize, duplicate));
  HalFile duplicateFile;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/.completion-journal/.cretire-02", duplicateFile));
  ASSERT_EQ(duplicateFile.write(duplicate.data(), duplicate.size()), duplicate.size());
  ASSERT_TRUE(duplicateFile.close());
  cj::RetiredJournalLocation location{};
  location.slot = 99;
  location.directory[0] = 'x';
  EXPECT_EQ(cj::retiredDeferredMoveProof(BOOK, origin, moveHash, &location), cj::DeferredRetirementProof::Conflict);
  EXPECT_EQ(location.slot, 99u);
  EXPECT_EQ(location.directory[0], 'x');
  EXPECT_EQ(completionmove::inspectRetiredPlan(BOOK, "/read/Book.epub", origin, moveHash),
            completionmove::RetiredPlanResult::Conflict);
  ASSERT_TRUE(Storage.remove("/.completion-journal/.completion-02"));
  ASSERT_TRUE(Storage.remove("/.completion-journal/.cseal-02"));
  ASSERT_TRUE(Storage.remove("/.completion-journal/.cretire-02"));
  EXPECT_EQ(cj::retiredDeferredMoveProof(BOOK, origin ^ 1, moveHash), cj::DeferredRetirementProof::Missing);
  EXPECT_EQ(cj::retiredDeferredMoveProof(BOOK, origin, moveHash ^ 1), cj::DeferredRetirementProof::Missing);
  EXPECT_EQ(cj::retiredDeferredMoveProof("/other.epub", origin, moveHash), cj::DeferredRetirementProof::Missing);
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/.completion-journal/.cmoveseal-01", file));
  const uint8_t badSeal = 0;
  ASSERT_EQ(file.write(&badSeal, 1), 1u);
  ASSERT_TRUE(file.close());
  EXPECT_EQ(completionmove::inspectRetiredPlan(BOOK, "/read/Book.epub", origin, moveHash),
            completionmove::RetiredPlanResult::Damaged);
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/.completion-journal/.cretire-01", file));
  const uint8_t bad = 0;
  ASSERT_EQ(file.write(&bad, 1), 1u);
  ASSERT_TRUE(file.close());
  EXPECT_EQ(cj::retiredDeferredMoveProof(BOOK, origin, moveHash), cj::DeferredRetirementProof::Damaged);
}

TEST_F(DeferredCompletionIntentIO, FindsExactRetiredMoveAfterRollover) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  dc::Record intent{};
  size_t intentSize = 0;
  unsigned intentSlot = 0;
  ASSERT_EQ(dc::findReady(BOOK, intent, intentSize, intentSlot), dc::FindResult::Found);
  uint64_t origin = 0;
  std::string_view source;
  ASSERT_TRUE(dc::decodeWithHash(intent.data(), intentSize, origin, source));
  ASSERT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Prepared);
  cj::Record move{};
  size_t moveSize = 0;
  unsigned moveSlot = 0;
  ASSERT_EQ(cj::scanRecords(move, moveSize, moveSlot), cj::ScanResult::Candidate);
  const auto moveHash = cj::checksum(move.data(), moveSize);
  ASSERT_EQ(cj::retire(), cj::RetirementResult::Retired);
  for (int i = 0; i < 63; ++i) {
    const bool completed = (i % 2) != 0;
    ASSERT_EQ(setClosedBookCompletionStats(BOOK, completed, {2026, 11, 11}), ClosedCompletionResult::Applied) << i;
    ASSERT_EQ(finalizeClosedBookCompletion(BOOK, completed), CompletionFinalizeResult::Finalized) << i;
  }
  ASSERT_TRUE(Storage.exists("/.completion-history-000000/.archive-00"));
  ASSERT_TRUE(Storage.rename("/.completion-history-000000", "/.completion-history-000002"));
  EXPECT_EQ(cj::retiredDeferredMoveProof(BOOK, origin, moveHash), cj::DeferredRetirementProof::Retired);
  EXPECT_EQ(completionmove::inspectRetiredPlan(BOOK, "/read/Book.epub", origin, moveHash),
            completionmove::RetiredPlanResult::Ready);
  ASSERT_EQ(cj::createRecord(1, move, moveSize), cj::CreateResult::Created);
  ASSERT_EQ(cj::createSeal(1, move, moveSize), cj::CreateResult::Created);
  cj::Seal duplicate{};
  ASSERT_TRUE(cj::encodeRetirement(1, move, moveSize, duplicate));
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/.completion-journal/.cretire-01", file));
  ASSERT_EQ(file.write(duplicate.data(), duplicate.size()), duplicate.size());
  ASSERT_TRUE(file.close());
  EXPECT_EQ(cj::retiredDeferredMoveProof(BOOK, origin, moveHash), cj::DeferredRetirementProof::Conflict);
}

TEST_F(DeferredCompletionIntentIO, VerifiesMovedFilesAgainstRetiredPlan) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  dc::Record intent{};
  size_t intentSize = 0;
  unsigned intentSlot = 0;
  ASSERT_EQ(dc::findReady(BOOK, intent, intentSize, intentSlot), dc::FindResult::Found);
  uint64_t origin = 0;
  std::string_view source;
  ASSERT_TRUE(dc::decodeWithHash(intent.data(), intentSize, origin, source));
  ASSERT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Prepared);
  cj::Record move{};
  size_t moveSize = 0;
  unsigned moveSlot = 0;
  ASSERT_EQ(cj::scanRecords(move, moveSize, moveSlot), cj::ScanResult::Candidate);
  const auto moveHash = cj::checksum(move.data(), moveSize);
  ASSERT_EQ(completionmove::movePreparedFiles(), completionmove::MoveFilesResult::Moved);
  ASSERT_EQ(dc::retireMovedIntent(BOOK), dc::RetireMoveResult::Retired);
  ASSERT_EQ(cj::retire(), cj::RetirementResult::Retired);
  EXPECT_EQ(completionmove::verifyRetiredFiles(BOOK, "/read/Book.epub", origin, moveHash),
            completionmove::VerifyFilesResult::ContentMatches);
  EXPECT_EQ(completionmove::verifyRetiredFiles(BOOK, "/read/Other.epub", origin, moveHash),
            completionmove::VerifyFilesResult::NotReady);
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/read/Book.epub", file));
  const uint8_t changed = 7;
  ASSERT_EQ(file.write(&changed, 1), 1u);
  ASSERT_TRUE(file.close());
  EXPECT_EQ(completionmove::verifyRetiredFiles(BOOK, "/read/Book.epub", origin, moveHash),
            completionmove::VerifyFilesResult::NotReady);
}

TEST_F(DeferredCompletionIntentIO, FailedMovedIntentRetirementKeepsJournalAndBook) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  ASSERT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Prepared);
  ASSERT_EQ(completionmove::movePreparedFiles(), completionmove::MoveFilesResult::Moved);
  Storage.failNextWriteFor("/.completion-deferred/.dretire-00", 3);
  EXPECT_EQ(dc::retireMovedIntent(BOOK), dc::RetireMoveResult::Unavailable);
  dc::Record intent{};
  size_t size = 0;
  unsigned slot = 99;
  EXPECT_EQ(dc::find(BOOK, intent, size, slot), dc::FindResult::Blocked);
  Storage.failNextRenameFrom("/.completion-deferred/.dretire-00");
  EXPECT_EQ(dc::retireMovedIntent(BOOK), dc::RetireMoveResult::Unavailable);
  EXPECT_TRUE(Storage.exists("/.completion-deferred/.dretire-00"));
  EXPECT_EQ(dc::retireMovedIntent(BOOK), dc::RetireMoveResult::Retired);
  EXPECT_TRUE(Storage.exists("/.completion-deferred/.dbad-00-00"));
  EXPECT_EQ(dc::retireMovedIntent(BOOK), dc::RetireMoveResult::AlreadyRetired);
  EXPECT_TRUE(Storage.exists("/read/Book.epub"));
  bool completed = false;
  EXPECT_EQ(probeClosedBookCompletion("/read/Book.epub", completed), CompletionProbeResult::Matching);
}

TEST_F(DeferredCompletionIntentIO, MovedStateIsNotVerifiedWhenChainsCannotBeProvenDistinct) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  ASSERT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Prepared);
  ASSERT_EQ(completionmove::movePreparedFiles(), completionmove::MoveFilesResult::Moved);
  Storage.failSettingsIdentity();
  EXPECT_EQ(completionmove::verifyPreparedFiles(), completionmove::VerifyFilesResult::NotReady);
  EXPECT_TRUE(Storage.exists("/read/Book.epub"));
  EXPECT_EQ(finalizeClosedBookCompletion("/read/Book.epub", true), CompletionFinalizeResult::ManualReview);
}

TEST_F(DeferredCompletionIntentIO, MoveDoesNotStartWhenLibraryDirtyMarkerCannotBeWritten) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  ASSERT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Prepared);
  Storage.failNextWriteFor("/.crosspoint/library.dirty", 0);
  EXPECT_EQ(completionmove::movePreparedFiles(), completionmove::MoveFilesResult::RecoveryRequired);
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_FALSE(Storage.exists("/read/Book.epub"));
  EXPECT_EQ(completionmove::movePreparedFiles(), completionmove::MoveFilesResult::Moved);
}

TEST_F(DeferredCompletionIntentIO, MoveDoesNotStartWhenDirtyMarkerIsDirectory) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  ASSERT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Prepared);
  ASSERT_TRUE(Storage.mkdir("/.crosspoint/library.dirty"));
  EXPECT_EQ(completionmove::movePreparedFiles(), completionmove::MoveFilesResult::RecoveryRequired);
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_FALSE(Storage.exists("/read/Book.epub"));
}

TEST_F(DeferredCompletionIntentIO, MovePreparationRefusesExistingDestinationBeforeJournal) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  ASSERT_TRUE(Storage.mkdir("/read"));
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/read/Book.epub", file));
  ASSERT_TRUE(file.close());
  EXPECT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Blocked);
  EXPECT_EQ(dc::assess(BOOK), dc::MoveAssessment::Ready);
  EXPECT_TRUE(Storage.exists(BOOK));
}

TEST_F(DeferredCompletionIntentIO, MovePreparationRefusesAFileNamedReadFolder) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/read", file));
  ASSERT_TRUE(file.close());
  EXPECT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Blocked);
  EXPECT_EQ(dc::assess(BOOK), dc::MoveAssessment::Ready);
  EXPECT_TRUE(Storage.exists(BOOK));
}

TEST_F(DeferredCompletionIntentIO, MovePreparationPreservesOriginalRecentsPolicy) {
  using namespace readingstats;
  ASSERT_EQ(setClosedBookCompletionStats(BOOK, true, {2026, 11, 11}, cj::DEFER_MOVE_TO_READ | cj::REMOVE_RECENTS),
            ClosedCompletionResult::Applied);
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  ASSERT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Prepared);
  bool completed = false;
  uint8_t policy = 0;
  ASSERT_EQ(probeClosedBookCompletion(BOOK, completed, &policy), CompletionProbeResult::Matching);
  EXPECT_EQ(policy, cj::MOVE_TO_READ | cj::MOVE_FROM_DEFERRED | cj::REMOVE_RECENTS);
}

TEST_F(DeferredCompletionIntentIO, MovePlanConflictKeepsJournalAndSourceForRecovery) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  ASSERT_TRUE(Storage.mkdir("/read"));
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/read/Book.epub.key", file));
  ASSERT_TRUE(file.close());
  EXPECT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::RecoveryRequired);
  bool completed = false;
  uint8_t policy = 0;
  ASSERT_EQ(probeClosedBookCompletion(BOOK, completed, &policy), CompletionProbeResult::Matching);
  EXPECT_EQ(policy, cj::MOVE_TO_READ | cj::MOVE_FROM_DEFERRED);
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_TRUE(Storage.exists("/read/Book.epub.key"));
  EXPECT_FALSE(Storage.exists("/read/Book.epub"));
  ASSERT_TRUE(Storage.remove("/read/Book.epub.key"));
  EXPECT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Prepared);
}

TEST_F(DeferredCompletionIntentIO, RefusesToResumeMoveForDifferentRequestToken) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  dc::Record intent{};
  size_t intentSize = 0;
  unsigned intentSlot = 0;
  ASSERT_EQ(dc::findReady(BOOK, intent, intentSize, intentSlot), dc::FindResult::Found);
  std::string_view source;
  uint64_t origin = 0;
  ASSERT_TRUE(dc::decodeWithHash(intent.data(), intentSize, origin, source));
  ASSERT_EQ(setClosedBookDeferredMoveStats(BOOK, {2026, 11, 11}, origin ^ 1, false), ClosedCompletionResult::Applied);
  EXPECT_EQ(dc::findForMoveRecovery(BOOK, intent, intentSize, intentSlot), dc::FindResult::Missing);
  EXPECT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::RecoveryRequired);
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_FALSE(Storage.exists("/read/Book.epub"));
}

TEST_F(DeferredCompletionIntentIO, MovePreparationCancelsIfBookWasUnfinished) {
  using namespace readingstats;
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  ASSERT_EQ(setClosedBookCompletionStats(BOOK, false, {2026, 11, 11}), ClosedCompletionResult::Applied);
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, false), CompletionFinalizeResult::Finalized);
  EXPECT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Cancelled);
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_EQ(dc::assess(BOOK), dc::MoveAssessment::Missing);
}

TEST_F(DeferredCompletionIntentIO, CancelledRequestLeavesTheNextFinishedBookResumable) {
  using namespace readingstats;
  constexpr char other[] = "/Authors/Other/Second.epub";
  start();
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", other, file));
  ASSERT_TRUE(file.close());
  const auto cache = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(other));
  ASSERT_TRUE(saveBook(bookDirectory(cache), BookReadingStats{}));
  ASSERT_EQ(setClosedBookCompletionStats(other, true, {2026, 11, 11}, cj::DEFER_MOVE_TO_READ),
            ClosedCompletionResult::Applied);
  ASSERT_EQ(finalizeClosedBookCompletion(other, true), CompletionFinalizeResult::Finalized);
  ASSERT_EQ(setClosedBookCompletionStats(BOOK, false, {2026, 11, 11}), ClosedCompletionResult::Applied);
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, false), CompletionFinalizeResult::Finalized);
  ASSERT_EQ(dc::prepareMove(BOOK, "/read/Book.epub", {2026, 11, 11}), dc::PrepareMoveResult::Cancelled);
  auto record = std::make_unique<dc::Record>();
  size_t size = 0;
  unsigned slot = 0;
  ASSERT_EQ(dc::discoverResumable(*record, size, slot), dc::FindResult::Found);
  uint64_t origin = 0;
  std::string_view path;
  ASSERT_TRUE(dc::decodeWithHash(record->data(), size, origin, path));
  EXPECT_EQ(path, other);
  ASSERT_EQ(dc::prepareMove(other, "/read/Second.epub", {2026, 11, 11}), dc::PrepareMoveResult::Prepared);
  ASSERT_EQ(completionmove::movePreparedFiles(), completionmove::MoveFilesResult::Moved);
  ASSERT_EQ(completionmove::verifyPreparedFiles(), completionmove::VerifyFilesResult::ContentMatches);
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_FALSE(Storage.exists("/read/Book.epub"));
  EXPECT_FALSE(Storage.exists(other));
  EXPECT_TRUE(Storage.exists("/read/Second.epub"));
  // The second request remains recoverable until references are finalized.
  EXPECT_EQ(dc::discoverResumable(*record, size, slot), dc::FindResult::Found);
}

TEST_F(DeferredCompletionIntentIO, RefusesMissingMismatchedAndActiveTransactions) {
  EXPECT_EQ(dc::publish(BOOK), dc::PublishResult::NoPending);
  start();
  EXPECT_EQ(dc::publish("/other.epub"), dc::PublishResult::Conflict);
  StatsSessionLease lease;
  EXPECT_FALSE(lease.acquire());  // A pending transaction already blocks a new reader.
  EXPECT_FALSE(Storage.exists("/.completion-deferred/.defer-00"));
}

TEST_F(DeferredCompletionIntentIO, PartialSealBlocksWithoutRetiringTheJournal) {
  start();
  Storage.failNextWriteFor("/.completion-deferred/.dseal-00", 0);
  ASSERT_EQ(dc::publish(BOOK), dc::PublishResult::Unavailable);
  EXPECT_TRUE(Storage.exists("/.completion-deferred/.defer-00"));
  EXPECT_TRUE(Storage.exists("/.completion-deferred/.dseal-00"));
  EXPECT_EQ(dc::publish(BOOK), dc::PublishResult::Blocked);
  EXPECT_EQ(readingstats::finalizeClosedBookCompletion(BOOK, true),
            readingstats::CompletionFinalizeResult::ManualReview);
}

TEST_F(DeferredCompletionIntentIO, RetriesACompleteRecordWithoutASeal) {
  start();
  Storage.failCheckedOpenAfter(1);
  ASSERT_EQ(dc::publish(BOOK), dc::PublishResult::Unavailable);
  EXPECT_TRUE(Storage.exists("/.completion-deferred/.defer-00"));
  EXPECT_FALSE(Storage.exists("/.completion-deferred/.dseal-00"));
  EXPECT_EQ(dc::publish(BOOK), dc::PublishResult::Published);
  EXPECT_EQ(dc::publish(BOOK), dc::PublishResult::AlreadyPublished);
}

TEST_F(DeferredCompletionIntentIO, DoesNotOverwriteAnIncompleteIntent) {
  start();
  Storage.failNextWriteFor("/.completion-deferred/.defer-00", 3);
  EXPECT_EQ(dc::publish(BOOK), dc::PublishResult::Unavailable);
  EXPECT_EQ(dc::publish(BOOK), dc::PublishResult::Blocked);
  EXPECT_TRUE(Storage.exists("/.completion-deferred/.defer-00"));
  EXPECT_FALSE(Storage.exists("/.completion-deferred/.dseal-00"));
}
