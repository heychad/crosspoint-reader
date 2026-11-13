#include <Arduino.h>
#include <BookReadingStats.h>
#include <BookTracking.h>
#include <CompletionJournalIO.h>
#include <CompletionMovePlanIO.h>
#include <CompletionTransaction.h>
#include <DeferredCompletionIntentIO.h>
#include <DeferredCompletionMove.h>
#include <GlobalReadingStats.h>
#include <HalStorage.h>
#include <Memory.h>
#include <ReaderStats.h>
#include <ReadingStatsStore.h>
#include <StatsSessionLease.h>
#include <gtest/gtest.h>

#include <functional>
#include <string>

namespace {
constexpr char BOOK[] = "/Authors/Writer/Example.epub";
std::string cachePath() { return "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(BOOK)); }
class ClosedCompletionTransaction : public testing::Test {
 protected:
  void SetUp() override {
    Storage.reset();
    HalFile file;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", BOOK, file));
    ASSERT_TRUE(file.close());
    BookReadingStats book;
    book.totalReadingSeconds = 1234;
    book.totalPagesTurned = 32;
    ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(cachePath()), book));
    GlobalReadingStats global;
    global.totalReadingSeconds = 5678;
    ASSERT_TRUE(global.save());
  }
};
}  // namespace

TEST_F(ClosedCompletionTransaction, RestoreSnapshotUpdatesBookAndGlobalCompletionTogether) {
  using namespace readingstats;
  completionjournal::Book current{}, replacement{};
  BookReadingStats old = BookReadingStats::load(bookDirectory(cachePath()));
  ASSERT_TRUE(old.encode(current.data(), current.size()));
  auto restored = old;
  restored.isCompleted = true;
  restored.totalReadingSeconds = 9000;
  ASSERT_TRUE(restored.encode(replacement.data(), replacement.size()));
  ASSERT_EQ(restoreClosedBookSnapshot(BOOK, cachePath(), current, replacement), ClosedCompletionResult::Applied);
  ASSERT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  const auto book = BookReadingStats::load(bookDirectory(cachePath()));
  const auto global = GlobalReadingStats::loadReadOnly();
  EXPECT_TRUE(book.writable && book.isCompleted);
  EXPECT_EQ(book.totalReadingSeconds, 9000U);
  EXPECT_EQ(global.completedBooks, 1U);
  EXPECT_EQ(global.totalReadingSeconds, 5678U);
}

TEST_F(ClosedCompletionTransaction, RestoreSnapshotRejectsWrongCacheAndChangedBaseline) {
  using namespace readingstats;
  completionjournal::Book current{}, replacement{};
  auto book = BookReadingStats::load(bookDirectory(cachePath()));
  ASSERT_TRUE(book.encode(current.data(), current.size()));
  book.isCompleted = true;
  ASSERT_TRUE(book.encode(replacement.data(), replacement.size()));
  EXPECT_EQ(restoreClosedBookSnapshot(BOOK, "/.crosspoint/epub_other", current, replacement),
            ClosedCompletionResult::Invalid);
  current[3] ^= 1;
  EXPECT_EQ(restoreClosedBookSnapshot(BOOK, cachePath(), current, replacement), ClosedCompletionResult::Invalid);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().completedBooks, 0U);
  EXPECT_FALSE(BookReadingStats::load(bookDirectory(cachePath())).isCompleted);
}

TEST_F(ClosedCompletionTransaction, RefusesToRetireUnverifiedBookMove) {
  namespace cj = readingstats::completionjournal;
  ASSERT_EQ(readingstats::setClosedBookCompletionStats(BOOK, true, ReadingStatsDate{2026, 11, 10}, cj::MOVE_TO_READ),
            readingstats::ClosedCompletionResult::Applied);
  EXPECT_EQ(readingstats::finalizeClosedBookCompletion(BOOK, true),
            readingstats::CompletionFinalizeResult::ManualReview);
  bool target = false;
  uint8_t policy = 0;
  EXPECT_EQ(readingstats::probeClosedBookCompletion(BOOK, target, &policy),
            readingstats::CompletionProbeResult::Matching);
  EXPECT_TRUE(target);
  EXPECT_EQ(policy, cj::MOVE_TO_READ);
}

TEST_F(ClosedCompletionTransaction, RejectsAmbiguousMoveSourceBeforeChangingStatsOrStartingJournal) {
  using namespace readingstats;
  namespace cj = completionjournal;
  const std::string source = "/Authors/Writer/Part~1.epub";
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", source, file));
  ASSERT_TRUE(file.close());
  const auto cache = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(source));
  BookReadingStats before;
  before.totalReadingSeconds = 1234;
  ASSERT_TRUE(saveBook(bookDirectory(cache), before));
  for (uint8_t policy : {cj::MOVE_TO_READ, cj::DEFER_MOVE_TO_READ}) {
    ASSERT_EQ(setClosedBookCompletionStats(source, true, {2026, 11, 13}, policy),
              ClosedCompletionResult::UnsupportedMoveSource);
    EXPECT_FALSE(Storage.exists("/.completion-journal"));
    EXPECT_TRUE(Storage.exists(source.c_str()));
    const auto after = BookReadingStats::load(bookDirectory(cache));
    EXPECT_TRUE(after.writable && !after.isCompleted);
    EXPECT_EQ(after.totalReadingSeconds, 1234U);
    EXPECT_EQ(GlobalReadingStats::loadReadOnly().completedBooks, 0U);
    StatsSessionLease reader;
    ASSERT_TRUE(reader.acquire() && reader.release());
  }
  ASSERT_EQ(setClosedBookCompletionStats(source, true, {2026, 11, 13}), ClosedCompletionResult::Applied);
  EXPECT_EQ(finalizeClosedBookCompletion(source, true), CompletionFinalizeResult::Finalized);
}

TEST_F(ClosedCompletionTransaction, DeferredMovePersistsSourceBeforeJournalRetirement) {
  namespace cj = readingstats::completionjournal;
  using namespace readingstats;
  ASSERT_EQ(setClosedBookCompletionStats(BOOK, true, {2026, 11, 10}, cj::DEFER_MOVE_TO_READ),
            ClosedCompletionResult::Applied);
  cj::Record record{};
  size_t size = 0;
  unsigned slot = 99;
  ASSERT_EQ(cj::scanRecords(record, size, slot), cj::ScanResult::Candidate);
  std::string_view source;
  ASSERT_TRUE(cj::decodeDeferredSource(record.data(), size, source));
  EXPECT_EQ(source, BOOK);
  bool target = false;
  uint8_t policy = 0;
  EXPECT_EQ(probeClosedBookCompletion(BOOK, target, &policy), CompletionProbeResult::Matching);
  EXPECT_TRUE(target);
  EXPECT_EQ(policy, cj::DEFER_MOVE_TO_READ);
  EXPECT_EQ(recoverClosedBookCompletionStats(BOOK, true), CompletionRecoveryResult::Recovered);
  EXPECT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  EXPECT_TRUE(Storage.exists("/.completion-deferred/.defer-00"));
  EXPECT_TRUE(Storage.exists("/.completion-deferred/.dseal-00"));
  EXPECT_EQ(probeClosedBookCompletion(BOOK, target), CompletionProbeResult::NoPending);
  EXPECT_TRUE(Storage.exists(BOOK));
}

TEST_F(ClosedCompletionTransaction, DeferredRecoveryRejectsMismatchedStoredSource) {
  namespace cj = readingstats::completionjournal;
  using namespace readingstats;
  ASSERT_TRUE(Storage.mkdir("/.completion-journal"));
  cj::Book book{};
  cj::Global global{};
  book[0] = 5;
  book[11] = 1;
  global[0] = 3;
  cj::Record record{};
  size_t size = 0;
  ASSERT_TRUE(cj::encode(bookDirectory(cachePath()), book, global, {}, record, size, cj::DEFER_MOVE_TO_READ,
                         "/different.epub"));
  ASSERT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Created);
  ASSERT_EQ(cj::createSeal(0, record, size), cj::CreateResult::Created);
  bool target = false;
  EXPECT_EQ(probeClosedBookCompletion(BOOK, target), CompletionProbeResult::DifferentTransaction);
  EXPECT_EQ(recoverClosedBookCompletionStats(BOOK, true), CompletionRecoveryResult::DifferentTransaction);
  EXPECT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::DifferentTransaction);
}

TEST_F(ClosedCompletionTransaction, MarksAndUnmarksWithoutChangingReadingTotals) {
  using readingstats::ClosedCompletionResult;
  namespace cj = readingstats::completionjournal;
  const ReadingStatsDate today{2026, 11, 10};
  EXPECT_EQ(readingstats::setClosedBookCompletionStats(BOOK, true, today, cj::REMOVE_RECENTS),
            ClosedCompletionResult::Applied);
  bool pendingTarget = false;
  uint8_t pendingPolicy = 0;
  EXPECT_EQ(readingstats::probeClosedBookCompletion(BOOK, pendingTarget, &pendingPolicy),
            readingstats::CompletionProbeResult::Matching);
  EXPECT_TRUE(pendingTarget);
  EXPECT_EQ(pendingPolicy, cj::REMOVE_RECENTS);
  EXPECT_EQ(readingstats::finalizeClosedBookCompletion(BOOK, true), readingstats::CompletionFinalizeResult::Finalized);
  auto book = BookReadingStats::load(readingstats::bookDirectory(cachePath()));
  auto global = GlobalReadingStats::loadReadOnly();
  ASSERT_TRUE(book.writable && global.writable);
  EXPECT_TRUE(book.isCompleted);
  EXPECT_EQ(book.totalReadingSeconds, 1234u);
  EXPECT_EQ(book.totalPagesTurned, 32u);
  EXPECT_EQ(book.finishedDate.year, 2026);
  EXPECT_EQ(global.completedBooks, 1u);
  EXPECT_EQ(global.totalReadingSeconds, 5678u);
  EXPECT_EQ(readingstats::setClosedBookCompletionStats(BOOK, true, today), ClosedCompletionResult::Unchanged);
  ASSERT_EQ(readingstats::setClosedBookCompletionStats(BOOK, true, today, cj::REMOVE_RECENTS),
            ClosedCompletionResult::Applied);
  pendingPolicy = 0;
  ASSERT_EQ(readingstats::probeClosedBookCompletion(BOOK, pendingTarget, &pendingPolicy),
            readingstats::CompletionProbeResult::Matching);
  EXPECT_EQ(pendingPolicy, cj::REMOVE_RECENTS);
  ASSERT_EQ(readingstats::recoverClosedBookCompletionStats(BOOK, true),
            readingstats::CompletionRecoveryResult::Recovered);
  ASSERT_EQ(readingstats::finalizeClosedBookCompletion(BOOK, true), readingstats::CompletionFinalizeResult::Finalized);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().completedBooks, 1u);
  EXPECT_EQ(readingstats::setClosedBookCompletionStats(BOOK, false, today, cj::REMOVE_RECENTS),
            ClosedCompletionResult::Invalid);
  EXPECT_EQ(readingstats::setClosedBookCompletionStats(BOOK, false, today), ClosedCompletionResult::Applied);
  EXPECT_EQ(readingstats::finalizeClosedBookCompletion(BOOK, false), readingstats::CompletionFinalizeResult::Finalized);
  book = BookReadingStats::load(readingstats::bookDirectory(cachePath()));
  global = GlobalReadingStats::loadReadOnly();
  EXPECT_FALSE(book.isCompleted);
  EXPECT_EQ(global.completedBooks, 0u);
  EXPECT_EQ(book.totalReadingSeconds, 1234u);
  EXPECT_EQ(global.totalReadingSeconds, 5678u);
  EXPECT_TRUE(Storage.exists(BOOK));
}

TEST_F(ClosedCompletionTransaction, RollsOverRetiredJournalAndKeepsUsingCompletion) {
  using namespace readingstats;
  for (int i = 0; i < 130; ++i) {
    const bool completed = (i % 2) == 0;
    ASSERT_EQ(setClosedBookCompletionStats(BOOK, completed, {2026, 11, 10}), ClosedCompletionResult::Applied) << i;
    ASSERT_EQ(finalizeClosedBookCompletion(BOOK, completed), CompletionFinalizeResult::Finalized) << i;
  }
  EXPECT_TRUE(Storage.exists("/.completion-history-000000/.archive-00"));
  EXPECT_TRUE(Storage.exists("/.completion-history-000001/.archive-00"));
  EXPECT_TRUE(Storage.exists("/.completion-journal/.completion-01"));
  EXPECT_FALSE(BookReadingStats::load(bookDirectory(cachePath())).isCompleted);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().completedBooks, 0u);
  EXPECT_TRUE(Storage.exists(BOOK));
}

TEST_F(ClosedCompletionTransaction, FailedArchiveRenamePreservesFullJournalForRetry) {
  using namespace readingstats;
  for (int i = 0; i < 64; ++i) {
    const bool completed = (i % 2) == 0;
    ASSERT_EQ(setClosedBookCompletionStats(BOOK, completed, {2026, 11, 10}), ClosedCompletionResult::Applied) << i;
    ASSERT_EQ(finalizeClosedBookCompletion(BOOK, completed), CompletionFinalizeResult::Finalized) << i;
  }
  Storage.failNextRenameFrom("/.completion-journal");
  EXPECT_EQ(setClosedBookCompletionStats(BOOK, true, {2026, 11, 10}), ClosedCompletionResult::Unavailable);
  EXPECT_TRUE(Storage.exists("/.completion-journal/.archive-00"));
  EXPECT_FALSE(Storage.exists("/.completion-history-000000"));
  EXPECT_FALSE(BookReadingStats::load(bookDirectory(cachePath())).isCompleted);
  ASSERT_EQ(setClosedBookCompletionStats(BOOK, true, {2026, 11, 10}), ClosedCompletionResult::Applied);
  EXPECT_EQ(finalizeClosedBookCompletion(BOOK, true), CompletionFinalizeResult::Finalized);
  EXPECT_TRUE(Storage.exists("/.completion-history-000000/.archive-00"));
}

TEST_F(ClosedCompletionTransaction, IncompleteArchiveMarkerBlocksRolloverWithoutChangingStats) {
  using namespace readingstats;
  for (int i = 0; i < 64; ++i) {
    const bool completed = (i % 2) == 0;
    ASSERT_EQ(setClosedBookCompletionStats(BOOK, completed, {2026, 11, 10}), ClosedCompletionResult::Applied) << i;
    ASSERT_EQ(finalizeClosedBookCompletion(BOOK, completed), CompletionFinalizeResult::Finalized) << i;
  }
  Storage.failNextWriteFor("/.completion-journal/.archive-00", 3);
  EXPECT_EQ(setClosedBookCompletionStats(BOOK, true, {2026, 11, 10}), ClosedCompletionResult::Unavailable);
  EXPECT_TRUE(Storage.exists("/.completion-journal/.archive-00"));
  EXPECT_EQ(setClosedBookCompletionStats(BOOK, true, {2026, 11, 10}), ClosedCompletionResult::RecoveryRequired);
  EXPECT_FALSE(Storage.exists("/.completion-history-000000"));
  EXPECT_FALSE(BookReadingStats::load(bookDirectory(cachePath())).isCompleted);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().completedBooks, 0u);
  EXPECT_TRUE(Storage.exists(BOOK));
}

TEST_F(ClosedCompletionTransaction, PendingFinalSlotIsNeverArchived) {
  using namespace readingstats;
  for (int i = 0; i < 63; ++i) {
    const bool completed = (i % 2) == 0;
    ASSERT_EQ(setClosedBookCompletionStats(BOOK, completed, {2026, 11, 10}), ClosedCompletionResult::Applied) << i;
    ASSERT_EQ(finalizeClosedBookCompletion(BOOK, completed), CompletionFinalizeResult::Finalized) << i;
  }
  ASSERT_EQ(setClosedBookCompletionStats(BOOK, false, {2026, 11, 10}), ClosedCompletionResult::Applied);
  EXPECT_EQ(setClosedBookCompletionStats(BOOK, true, {2026, 11, 10}), ClosedCompletionResult::RecoveryRequired);
  EXPECT_FALSE(Storage.exists("/.completion-journal/.archive-00"));
  EXPECT_FALSE(Storage.exists("/.completion-history-000000"));
  bool target = true;
  EXPECT_EQ(probeClosedBookCompletion(BOOK, target), CompletionProbeResult::Matching);
  EXPECT_FALSE(target);
  EXPECT_EQ(finalizeClosedBookCompletion(BOOK, false), CompletionFinalizeResult::Finalized);
  EXPECT_EQ(setClosedBookCompletionStats(BOOK, true, {2026, 11, 10}), ClosedCompletionResult::Applied);
}

TEST_F(ClosedCompletionTransaction, KeepsJournalWhenSecondStatisticsSaveFails) {
  Storage.failNextRenameFrom("/.crosspoint/global_stats.bin.tmp");
  EXPECT_EQ(readingstats::setClosedBookCompletionStats(BOOK, true, {2026, 11, 10}),
            readingstats::ClosedCompletionResult::RecoveryRequired);
  EXPECT_TRUE(Storage.exists("/.completion-journal/.completion-00"));
  EXPECT_TRUE(Storage.exists("/.completion-journal/.cseal-00"));
  EXPECT_TRUE(Storage.exists("/.crosspoint/global_stats.bin.tmp"));
  EXPECT_EQ(readingstats::setClosedBookCompletionStats(BOOK, true, {2026, 11, 10}),
            readingstats::ClosedCompletionResult::RecoveryRequired);
  EXPECT_TRUE(Storage.exists(BOOK));
}

TEST_F(ClosedCompletionTransaction, RefusesUnknownOrUnavailableInputs) {
  EXPECT_EQ(readingstats::setClosedBookCompletionStats("/not-a-book.pdf", true, {}),
            readingstats::ClosedCompletionResult::Invalid);
  Storage.failReadsFor("/.crosspoint/global_stats.bin");
  EXPECT_EQ(readingstats::setClosedBookCompletionStats(BOOK, true, {}),
            readingstats::ClosedCompletionResult::Unavailable);
  EXPECT_FALSE(Storage.exists("/.completion-journal/.completion-00"));
}

TEST_F(ClosedCompletionTransaction, RefusesEditsUntilLiveReaderIsDestroyed) {
  EXPECT_FALSE(StatsSessionLease::anyActive());
  {
    ReaderStats reader;
    reader.begin(cachePath(), BOOK, true, 300);
    ASSERT_FALSE(reader.unavailableAtStart());
    ASSERT_TRUE(StatsSessionLease::anyActive());
    EXPECT_EQ(readingstats::setClosedBookCompletionStats(BOOK, true, {}), readingstats::ClosedCompletionResult::Busy);
    EXPECT_TRUE(reader.finish());
    EXPECT_EQ(readingstats::setClosedBookCompletionStats(BOOK, true, {}), readingstats::ClosedCompletionResult::Busy);
    EXPECT_FALSE(Storage.exists("/.completion-journal/.completion-00"));
  }
  EXPECT_FALSE(StatsSessionLease::anyActive());
  EXPECT_EQ(readingstats::setClosedBookCompletionStats(BOOK, true, {}), readingstats::ClosedCompletionResult::Applied);
}

TEST_F(ClosedCompletionTransaction, LeaseReportsFailedReleaseAndRetainsEditProtection) {
  StatsSessionLease lease;
  ASSERT_TRUE(lease.acquire());
  Storage.failNextStorageLock();
  EXPECT_FALSE(lease.release());
  EXPECT_TRUE(StatsSessionLease::anyActive());
  EXPECT_EQ(readingstats::setClosedBookCompletionStats(BOOK, true, {}), readingstats::ClosedCompletionResult::Busy);
  EXPECT_TRUE(lease.release());
  EXPECT_FALSE(StatsSessionLease::anyActive());
  EXPECT_TRUE(lease.release());
  EXPECT_EQ(readingstats::setClosedBookCompletionStats(BOOK, true, {}), readingstats::ClosedCompletionResult::Applied);
}

TEST_F(ClosedCompletionTransaction, ExclusiveSuspensionRunsCompletedEditAndRestoresLease) {
  StatsSessionLease lease;
  ASSERT_TRUE(lease.acquire());
  struct Edit {
    readingstats::ClosedCompletionResult result = readingstats::ClosedCompletionResult::Unavailable;
  } edit;
  const auto apply = [](void* raw) {
    auto& state = *static_cast<Edit*>(raw);
    state.result = readingstats::setClosedBookCompletionStats(BOOK, true, {});
    return state.result == readingstats::ClosedCompletionResult::Applied &&
           readingstats::finalizeClosedBookCompletion(BOOK, true) == readingstats::CompletionFinalizeResult::Finalized;
  };
  EXPECT_EQ(lease.withExclusiveSuspension(apply, &edit), StatsSessionLease::SuspensionResult::Completed);
  EXPECT_EQ(edit.result, readingstats::ClosedCompletionResult::Applied);
  EXPECT_TRUE(StatsSessionLease::anyActive());
  EXPECT_EQ(readingstats::setClosedBookCompletionStats(BOOK, false, {}), readingstats::ClosedCompletionResult::Busy);
  EXPECT_TRUE(lease.release());
}

TEST_F(ClosedCompletionTransaction, ExclusiveSuspensionLeavesLeaseReleasedForPendingRecovery) {
  StatsSessionLease lease;
  ASSERT_TRUE(lease.acquire());
  const auto leavePending = [](void*) {
    return readingstats::setClosedBookCompletionStats(BOOK, true, {}) == readingstats::ClosedCompletionResult::Applied;
  };
  EXPECT_EQ(lease.withExclusiveSuspension(leavePending, nullptr),
            StatsSessionLease::SuspensionResult::RecoveryRequired);
  EXPECT_FALSE(StatsSessionLease::anyActive());
  EXPECT_FALSE(lease.acquire());
  EXPECT_EQ(readingstats::finalizeClosedBookCompletion(BOOK, true), readingstats::CompletionFinalizeResult::Finalized);
  EXPECT_TRUE(lease.acquire());
  EXPECT_TRUE(lease.release());
}

TEST_F(ClosedCompletionTransaction, ExclusiveSuspensionRefusesOtherActiveSessions) {
  StatsSessionLease first, second;
  ASSERT_TRUE(first.acquire());
  ASSERT_TRUE(second.acquire());
  bool called = false;
  const auto operation = [](void* raw) {
    *static_cast<bool*>(raw) = true;
    return true;
  };
  EXPECT_EQ(first.withExclusiveSuspension(operation, &called), StatsSessionLease::SuspensionResult::Busy);
  EXPECT_FALSE(called);
  EXPECT_TRUE(StatsSessionLease::anyActive());
  EXPECT_TRUE(second.release());
  EXPECT_TRUE(first.release());
}

TEST_F(ClosedCompletionTransaction, ExclusiveSuspensionRestoresLeaseAfterCleanFailure) {
  StatsSessionLease lease;
  const auto operation = [](void*) { return false; };
  EXPECT_EQ(lease.withExclusiveSuspension(operation, nullptr), StatsSessionLease::SuspensionResult::NotHeld);
  ASSERT_TRUE(lease.acquire());
  Storage.failNextStorageLock();
  EXPECT_EQ(lease.withExclusiveSuspension(operation, nullptr), StatsSessionLease::SuspensionResult::Unavailable);
  EXPECT_TRUE(StatsSessionLease::anyActive());
  EXPECT_EQ(lease.withExclusiveSuspension(operation, nullptr), StatsSessionLease::SuspensionResult::OperationFailed);
  EXPECT_TRUE(StatsSessionLease::anyActive());
  EXPECT_TRUE(lease.release());
}

TEST_F(ClosedCompletionTransaction, LiveCompletionPreservesOneSessionAndLaterReading) {
  testMillis = 0;
  ReaderStats reader;
  reader.begin(cachePath(), BOOK, true, 300);
  reader.onRender(ReadingStatsFrame::Page);
  testMillis = 65000;
  reader.noteTurn(true, true);
  reader.onRender(ReadingStatsFrame::Page);
  ASSERT_EQ(reader.editCompletionStats(cachePath(), BOOK, true, {2026, 11, 10}),
            ReaderStats::CompletionResult::Applied);
  EXPECT_TRUE(StatsSessionLease::anyActive());
  auto stored = BookReadingStats::load(readingstats::bookDirectory(cachePath()));
  EXPECT_TRUE(stored.isCompleted);
  EXPECT_EQ(stored.totalReadingSeconds, 1299U);
  reader.onRender(ReadingStatsFrame::Page);
  testMillis = 130000;
  reader.noteTurn(true, true);
  reader.onRender(ReadingStatsFrame::Page);
  ASSERT_TRUE(reader.finish());
  stored = BookReadingStats::load(readingstats::bookDirectory(cachePath()));
  const auto totals = GlobalReadingStats::loadReadOnly();
  EXPECT_TRUE(stored.isCompleted);
  EXPECT_EQ(stored.totalReadingSeconds, 1364U);
  EXPECT_EQ(stored.sessionCount, 1U);
  EXPECT_EQ(totals.totalReadingSeconds, 5808U);
  EXPECT_EQ(totals.totalSessions, 1U);
  EXPECT_EQ(totals.completedBooks, 1U);
}

TEST_F(ClosedCompletionTransaction, LiveCompletionPersistsDeferredMoveAfterReaderCloses) {
  namespace dc = readingstats::deferredcompletion;
  dc::Record record{};
  size_t size = 0;
  unsigned slot = 0;
  {
    ReaderStats reader;
    reader.begin(cachePath(), BOOK, true, 300);
    ASSERT_EQ(reader.editCompletionStats(cachePath(), BOOK, true, {}, false, nullptr, nullptr, true),
              ReaderStats::CompletionResult::Applied);
    EXPECT_TRUE(StatsSessionLease::anyActive());
    EXPECT_TRUE(Storage.exists(BOOK));
    EXPECT_EQ(dc::findReady(BOOK, record, size, slot), dc::FindResult::Busy);
    ASSERT_TRUE(reader.finish());
  }
  EXPECT_EQ(dc::findReady(BOOK, record, size, slot), dc::FindResult::Found);
  EXPECT_TRUE(Storage.exists(BOOK));
  ASSERT_EQ(dc::prepareMove(BOOK, "/read/Example.epub", {}), dc::PrepareMoveResult::Prepared);
  EXPECT_TRUE(Storage.exists(BOOK));
  ASSERT_EQ(readingstats::completionmove::movePreparedFiles(), readingstats::completionmove::MoveFilesResult::Moved);
  EXPECT_FALSE(Storage.exists(BOOK));
  EXPECT_TRUE(Storage.exists("/read/Example.epub"));
  EXPECT_EQ(readingstats::completionmove::verifyPreparedFiles(),
            readingstats::completionmove::VerifyFilesResult::ContentMatches);
}

TEST_F(ClosedCompletionTransaction, LiveDeferredMovePreservesBookSidecarsAndPosition) {
  namespace dc = readingstats::deferredcompletion;
  namespace cm = readingstats::completionmove;
  const std::string destination = "/read/Example.epub";
  const std::string newCache = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(destination));
  const auto write = [](const std::string& path, const std::string& data) {
    HalFile file;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", path, file));
    ASSERT_EQ(file.write(reinterpret_cast<const uint8_t*>(data.data()), data.size()), data.size());
    ASSERT_TRUE(file.close());
  };
  const auto read = [](const std::string& path) {
    HalFile file;
    if (!Storage.openFileForRead("TEST", path, file)) return std::string{};
    std::string data(file.fileSize(), '\0');
    if (file.read(reinterpret_cast<uint8_t*>(data.data()), data.size()) != static_cast<int>(data.size()) ||
        !file.close())
      return std::string{};
    return data;
  };
  write(BOOK, "epub content");
  write(std::string(BOOK) + ".key", "key content");
  write(std::string(BOOK) + ".rights", "rights content");
  write(cachePath() + "/progress.bin", "saved position");
  {
    ReaderStats reader;
    reader.begin(cachePath(), BOOK, true, 300);
    ASSERT_EQ(reader.editCompletionStats(cachePath(), BOOK, true, {}, false, nullptr, nullptr, true),
              ReaderStats::CompletionResult::Applied);
    EXPECT_EQ(dc::prepareMove(BOOK, destination, {}), dc::PrepareMoveResult::Busy);
    dc::Record pending{};
    size_t pendingSize = 0;
    unsigned pendingSlot = 99;
    EXPECT_EQ(dc::discoverResumable(pending, pendingSize, pendingSlot), dc::FindResult::Busy);
    EXPECT_TRUE(Storage.exists(BOOK));
    EXPECT_FALSE(Storage.exists(destination.c_str()));
    ASSERT_TRUE(reader.finish());
  }
  dc::Record pending{};
  size_t pendingSize = 0;
  unsigned pendingSlot = 99;
  ASSERT_EQ(dc::discoverResumable(pending, pendingSize, pendingSlot), dc::FindResult::Found);
  uint64_t completionHash = 0;
  std::string_view pendingSource;
  ASSERT_TRUE(dc::decodeWithHash(pending.data(), pendingSize, completionHash, pendingSource));
  EXPECT_EQ(pendingSource, BOOK);
  ASSERT_EQ(dc::prepareMove(BOOK, destination, {}), dc::PrepareMoveResult::Prepared);
  Storage.failNextRenameFrom(std::string(BOOK) + ".key");
  EXPECT_EQ(cm::movePreparedFiles(), cm::MoveFilesResult::RecoveryRequired);
  EXPECT_EQ(read(std::string(BOOK) + ".key"), "key content");
  EXPECT_EQ(read(cachePath() + "/progress.bin"), "saved position");
  ASSERT_EQ(cm::movePreparedFiles(), cm::MoveFilesResult::Moved);
  ASSERT_EQ(cm::verifyPreparedFiles(), cm::VerifyFilesResult::ContentMatches);
  EXPECT_EQ(read(destination), "epub content");
  EXPECT_EQ(read(destination + ".key"), "key content");
  EXPECT_EQ(read(destination + ".rights"), "rights content");
  EXPECT_EQ(read(newCache + "/progress.bin"), "saved position");
  EXPECT_EQ(read(cachePath() + "/progress.bin"), "saved position");
  const auto statsBytes = read(readingstats::bookDirectory(newCache) + "/stats_v5.bin");
  BookReadingStats stats;
  ASSERT_TRUE(BookReadingStats::decode(reinterpret_cast<const uint8_t*>(statsBytes.data()), statsBytes.size(), stats));
  EXPECT_TRUE(stats.isCompleted);
  EXPECT_EQ(stats.totalReadingSeconds, 1234U);
  const auto globalBytes = read("/.crosspoint/global_stats.bin");
  GlobalReadingStats global;
  ASSERT_TRUE(
      GlobalReadingStats::decode(reinterpret_cast<const uint8_t*>(globalBytes.data()), globalBytes.size(), global));
  EXPECT_EQ(global.completedBooks, 1U);
}

TEST_F(ClosedCompletionTransaction, LiveDeferredCompletionRecoversAfterRecentsFailure) {
  namespace dc = readingstats::deferredcompletion;
  dc::Record record{};
  size_t size = 0;
  unsigned slot = 0;
  {
    ReaderStats reader;
    reader.begin(cachePath(), BOOK, true, 300);
    const auto fail = [](const std::string&, void*) { return false; };
    ASSERT_EQ(reader.editCompletionStats(cachePath(), BOOK, true, {}, true, fail, nullptr, true),
              ReaderStats::CompletionResult::RecoveryRequired);
    EXPECT_FALSE(StatsSessionLease::anyActive());
    const auto remove = [](const std::string&, void*) { return true; };
    ASSERT_EQ(reader.recoverCompletionStats(cachePath(), BOOK, remove), ReaderStats::CompletionResult::Applied);
    EXPECT_TRUE(StatsSessionLease::anyActive());
    ASSERT_TRUE(reader.finish());
  }
  EXPECT_EQ(dc::findReady(BOOK, record, size, slot), dc::FindResult::Found);
  EXPECT_TRUE(Storage.exists(BOOK));
}

TEST_F(ClosedCompletionTransaction, UnfinishedBookCancelsDeferredMoveAfterClose) {
  namespace dc = readingstats::deferredcompletion;
  {
    ReaderStats reader;
    reader.begin(cachePath(), BOOK, true, 300);
    ASSERT_EQ(reader.editCompletionStats(cachePath(), BOOK, true, {}, false, nullptr, nullptr, true),
              ReaderStats::CompletionResult::Applied);
    ASSERT_EQ(reader.editCompletionStats(cachePath(), BOOK, false, {}), ReaderStats::CompletionResult::Applied);
    ASSERT_TRUE(reader.finish());
  }
  EXPECT_EQ(dc::prepareMove(BOOK, "/read/Example.epub", {}), dc::PrepareMoveResult::Cancelled);
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_FALSE(Storage.exists("/read/Example.epub"));
}

TEST_F(ClosedCompletionTransaction, LiveCompletionSaveFailureKeepsSessionAndRefusesEdit) {
  testMillis = 0;
  ReaderStats reader;
  reader.begin(cachePath(), BOOK, true, 300);
  reader.onRender(ReadingStatsFrame::Page);
  testMillis = 65000;
  reader.noteTurn(true, true);
  reader.onRender(ReadingStatsFrame::Page);
  Storage.failNextRenameFrom(readingstats::bookDirectory(cachePath()) + "/stats_v5.bin.tmp");
  EXPECT_EQ(reader.editCompletionStats(cachePath(), BOOK, true, {}), ReaderStats::CompletionResult::SaveFailed);
  EXPECT_TRUE(StatsSessionLease::anyActive());
  EXPECT_FALSE(BookReadingStats::load(readingstats::bookDirectory(cachePath())).isCompleted);
  EXPECT_FALSE(reader.finish());
  const auto temporary = readingstats::bookDirectory(cachePath()) + "/stats_v5.bin.tmp";
  ASSERT_TRUE(Storage.exists(temporary.c_str()));
  ASSERT_TRUE(Storage.remove(temporary.c_str()));  // Simulate resolving the injected temporary-file conflict.
  ASSERT_TRUE(reader.finish());
  const auto stored = BookReadingStats::load(readingstats::bookDirectory(cachePath()));
  EXPECT_FALSE(stored.isCompleted);
  EXPECT_EQ(stored.totalReadingSeconds, 1299U);
  EXPECT_EQ(stored.sessionCount, 1U);
}

TEST_F(ClosedCompletionTransaction, LiveCompletionRejectsDifferentBookOrDisabledTracking) {
  ReaderStats disabled;
  disabled.begin(cachePath(), BOOK, false, 300);
  EXPECT_EQ(disabled.editCompletionStats(cachePath(), BOOK, true, {}), ReaderStats::CompletionResult::NotTracking);
  ReaderStats reader;
  reader.begin(cachePath(), BOOK, true, 300);
  EXPECT_EQ(reader.editCompletionStats("/.crosspoint/epub_wrong", BOOK, true, {}),
            ReaderStats::CompletionResult::Unavailable);
  EXPECT_FALSE(BookReadingStats::load(readingstats::bookDirectory(cachePath())).isCompleted);
}

TEST_F(ClosedCompletionTransaction, LiveCompletionCreatesInitialStatisticsWhenSessionIsClean) {
  ASSERT_TRUE(Storage.remove((readingstats::bookDirectory(cachePath()) + "/stats_v5.bin").c_str()));
  ASSERT_TRUE(Storage.remove("/.crosspoint/global_stats.bin"));
  ReaderStats reader;
  reader.begin(cachePath(), BOOK, true, 300);
  ASSERT_EQ(reader.editCompletionStats(cachePath(), BOOK, true, {}), ReaderStats::CompletionResult::Applied);
  EXPECT_TRUE(BookReadingStats::load(readingstats::bookDirectory(cachePath())).isCompleted);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().completedBooks, 1U);
}

TEST_F(ClosedCompletionTransaction, LiveCompletionAllocationFailureLeavesSessionActive) {
  ReaderStats reader;
  reader.begin(cachePath(), BOOK, true, 300);
  failNextBackupAllocation = true;
  EXPECT_EQ(reader.editCompletionStats(cachePath(), BOOK, true, {}), ReaderStats::CompletionResult::Unavailable);
  EXPECT_TRUE(StatsSessionLease::anyActive());
  EXPECT_FALSE(BookReadingStats::load(readingstats::bookDirectory(cachePath())).isCompleted);
}

TEST_F(ClosedCompletionTransaction, LiveCompletionCanBeReversedWithoutDuplicatingSession) {
  testMillis = 0;
  ReaderStats reader;
  reader.begin(cachePath(), BOOK, true, 300);
  reader.onRender(ReadingStatsFrame::Page);
  testMillis = 65000;
  reader.noteTurn(true, true);
  reader.onRender(ReadingStatsFrame::Page);
  ASSERT_EQ(reader.editCompletionStats(cachePath(), BOOK, true, {}), ReaderStats::CompletionResult::Applied);
  ASSERT_EQ(reader.editCompletionStats(cachePath(), BOOK, false, {}), ReaderStats::CompletionResult::Applied);
  ASSERT_TRUE(reader.finish());
  const auto book = BookReadingStats::load(readingstats::bookDirectory(cachePath()));
  const auto global = GlobalReadingStats::loadReadOnly();
  EXPECT_FALSE(book.isCompleted);
  EXPECT_EQ(book.totalReadingSeconds, 1299U);
  EXPECT_EQ(book.sessionCount, 1U);
  EXPECT_EQ(global.completedBooks, 0U);
  EXPECT_EQ(global.totalSessions, 1U);
}

TEST_F(ClosedCompletionTransaction, LiveCompletionStopsTrackingWhenJournalNeedsRecovery) {
  testMillis = 0;
  ReaderStats reader;
  reader.begin(cachePath(), BOOK, true, 300);
  reader.onRender(ReadingStatsFrame::Page);
  testMillis = 65000;
  reader.noteTurn(true, true);
  reader.onRender(ReadingStatsFrame::Page);
  Storage.failNextNewSyncFor("/.completion-journal/.cseal-00");
  EXPECT_EQ(reader.editCompletionStats(cachePath(), BOOK, true, {}), ReaderStats::CompletionResult::RecoveryRequired);
  EXPECT_TRUE(reader.unavailableAtStart());
  EXPECT_FALSE(StatsSessionLease::anyActive());
  EXPECT_EQ(reader.recoverCompletionStats("/.crosspoint/epub_wrong", BOOK), ReaderStats::CompletionResult::Unavailable);
  ASSERT_EQ(reader.recoverCompletionStats(cachePath(), BOOK), ReaderStats::CompletionResult::Applied);
  EXPECT_FALSE(reader.unavailableAtStart());
  EXPECT_TRUE(StatsSessionLease::anyActive());
  const auto snapshot =
      BookReadingStats::load(readingstats::bookDirectory(cachePath()), BookReadingStats::LoadMode::ReadOnly);
  EXPECT_TRUE(snapshot.isCompleted);
  EXPECT_EQ(snapshot.totalReadingSeconds, 1299U);
  EXPECT_EQ(snapshot.sessionCount, 1U);
  reader.onRender(ReadingStatsFrame::Page);
  testMillis = 130000;
  reader.noteTurn(true, true);
  reader.onRender(ReadingStatsFrame::Page);
  ASSERT_TRUE(reader.finish());
  const auto finalBook = BookReadingStats::load(readingstats::bookDirectory(cachePath()));
  const auto finalGlobal = GlobalReadingStats::loadReadOnly();
  EXPECT_TRUE(finalBook.isCompleted);
  EXPECT_EQ(finalBook.totalReadingSeconds, 1364U);
  EXPECT_EQ(finalBook.sessionCount, 1U);
  EXPECT_EQ(finalGlobal.totalReadingSeconds, 5808U);
  EXPECT_EQ(finalGlobal.totalSessions, 1U);
}

TEST_F(ClosedCompletionTransaction, LiveCompletionRemovesRecentsBeforeJournalRetirement) {
  ReaderStats reader;
  reader.begin(cachePath(), BOOK, true, 300);
  bool removed = false;
  const auto remove = [](const std::string& path, void* raw) {
    bool target = false;
    uint8_t policy = 0;
    EXPECT_EQ(readingstats::probeClosedBookCompletion(path, target, &policy),
              readingstats::CompletionProbeResult::Matching);
    EXPECT_TRUE(target);
    EXPECT_EQ(policy, readingstats::completionjournal::REMOVE_RECENTS);
    *static_cast<bool*>(raw) = true;
    return true;
  };
  EXPECT_EQ(reader.editCompletionStats(cachePath(), BOOK, true, {}, true, remove, &removed),
            ReaderStats::CompletionResult::Applied);
  EXPECT_TRUE(removed);
  EXPECT_TRUE(StatsSessionLease::anyActive());
  bool target = false;
  EXPECT_EQ(readingstats::probeClosedBookCompletion(BOOK, target), readingstats::CompletionProbeResult::NoPending);
  EXPECT_TRUE(BookReadingStats::load(readingstats::bookDirectory(cachePath())).isCompleted);
}

TEST_F(ClosedCompletionTransaction, LiveCompletionRetainsJournalIfRecentsRemovalFails) {
  ReaderStats reader;
  reader.begin(cachePath(), BOOK, true, 300);
  const auto fail = [](const std::string&, void*) { return false; };
  EXPECT_EQ(reader.editCompletionStats(cachePath(), BOOK, true, {}, true, fail),
            ReaderStats::CompletionResult::RecoveryRequired);
  EXPECT_TRUE(reader.unavailableAtStart());
  EXPECT_FALSE(StatsSessionLease::anyActive());
  bool target = false;
  uint8_t policy = 0;
  EXPECT_EQ(readingstats::probeClosedBookCompletion(BOOK, target, &policy),
            readingstats::CompletionProbeResult::Matching);
  EXPECT_TRUE(target);
  EXPECT_EQ(policy, readingstats::completionjournal::REMOVE_RECENTS);
  EXPECT_EQ(reader.recoverCompletionStats(cachePath(), BOOK, fail), ReaderStats::CompletionResult::RecoveryRequired);
  bool removed = false;
  const auto retryRemove = [](const std::string&, void* raw) {
    *static_cast<bool*>(raw) = true;
    return true;
  };
  ASSERT_EQ(reader.recoverCompletionStats(cachePath(), BOOK, retryRemove, &removed),
            ReaderStats::CompletionResult::Applied);
  EXPECT_TRUE(removed);
  EXPECT_TRUE(StatsSessionLease::anyActive());
  EXPECT_FALSE(reader.unavailableAtStart());
}

TEST_F(ClosedCompletionTransaction, LiveCompletionRequiresValidRecentsCallback) {
  ReaderStats reader;
  reader.begin(cachePath(), BOOK, true, 300);
  EXPECT_EQ(reader.editCompletionStats(cachePath(), BOOK, true, {}, true), ReaderStats::CompletionResult::Unavailable);
  const auto remove = [](const std::string&, void*) { return true; };
  EXPECT_EQ(reader.editCompletionStats(cachePath(), BOOK, false, {}, true, remove),
            ReaderStats::CompletionResult::Unavailable);
  EXPECT_FALSE(BookReadingStats::load(readingstats::bookDirectory(cachePath())).isCompleted);
}

TEST_F(ClosedCompletionTransaction, LiveCompletionWorksWhenGlobalTrackingIsDisabled) {
  testMillis = 0;
  ReaderStats reader;
  reader.begin(cachePath(), BOOK, false, 300, true);
  bool completed = true;
  ASSERT_TRUE(reader.completionStatus(completed));
  EXPECT_FALSE(completed);
  reader.onRender(ReadingStatsFrame::Page);
  testMillis = 65000;
  reader.noteTurn(true, true);
  reader.onRender(ReadingStatsFrame::Page);
  ASSERT_EQ(reader.editCompletionStats(cachePath(), BOOK, true, {}), ReaderStats::CompletionResult::Applied);
  ASSERT_TRUE(reader.completionStatus(completed));
  EXPECT_TRUE(completed);
  EXPECT_EQ(reader.editCompletionStats(cachePath(), BOOK, true, {}), ReaderStats::CompletionResult::Unchanged);
  ASSERT_TRUE(reader.finish());
  const auto book = BookReadingStats::load(readingstats::bookDirectory(cachePath()));
  const auto global = GlobalReadingStats::loadReadOnly();
  EXPECT_EQ(book.totalReadingSeconds, 1234U);
  EXPECT_EQ(book.sessionCount, 0U);
  EXPECT_EQ(global.totalReadingSeconds, 5678U);
  EXPECT_EQ(global.totalSessions, 0U);
  EXPECT_EQ(global.completedBooks, 1U);
  EXPECT_EQ(booktracking::state(cachePath(), BOOK), booktracking::State::Enabled);
}

TEST_F(ClosedCompletionTransaction, LiveCompletionPreservesPerBookTrackingDisabledMarker) {
  ASSERT_TRUE(booktracking::setEnabled(cachePath(), BOOK, false));
  ReaderStats reader;
  reader.begin(cachePath(), BOOK, true, 300, true);
  bool completed = true;
  ASSERT_TRUE(reader.completionStatus(completed));
  EXPECT_FALSE(completed);
  ASSERT_EQ(reader.editCompletionStats(cachePath(), BOOK, true, {}), ReaderStats::CompletionResult::Applied);
  EXPECT_EQ(booktracking::state(cachePath(), BOOK), booktracking::State::Disabled);
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(cachePath())).totalReadingSeconds, 1234U);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().totalReadingSeconds, 5678U);
}

TEST_F(ClosedCompletionTransaction, UntrackedBookCreatesNoStatisticsUntilMarked) {
  const auto primary = readingstats::bookDirectory(cachePath()) + "/stats_v5.bin";
  ASSERT_TRUE(Storage.remove(primary.c_str()));
  ASSERT_TRUE(Storage.remove("/.crosspoint/global_stats.bin"));
  ReaderStats reader;
  reader.begin(cachePath(), BOOK, false, 300, true);
  bool completed = true;
  ASSERT_TRUE(reader.completionStatus(completed));
  EXPECT_FALSE(completed);
  EXPECT_FALSE(Storage.exists(primary.c_str()));
  EXPECT_FALSE(Storage.exists("/.crosspoint/global_stats.bin"));
  ASSERT_EQ(reader.editCompletionStats(cachePath(), BOOK, true, {}), ReaderStats::CompletionResult::Applied);
  EXPECT_TRUE(BookReadingStats::load(readingstats::bookDirectory(cachePath())).isCompleted);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().completedBooks, 1U);
  EXPECT_EQ(booktracking::state(cachePath(), BOOK), booktracking::State::Enabled);
}

TEST_F(ClosedCompletionTransaction, UntrackedCompletionRecoveryDoesNotStartTracking) {
  ReaderStats reader;
  reader.begin(cachePath(), BOOK, false, 300, true);
  Storage.failNextNewSyncFor("/.completion-journal/.cseal-00");
  EXPECT_EQ(reader.editCompletionStats(cachePath(), BOOK, true, {}), ReaderStats::CompletionResult::RecoveryRequired);
  ASSERT_EQ(reader.recoverCompletionStats(cachePath(), BOOK), ReaderStats::CompletionResult::Applied);
  bool completed = false;
  ASSERT_TRUE(reader.completionStatus(completed));
  EXPECT_TRUE(completed);
  testMillis = 0;
  reader.onRender(ReadingStatsFrame::Page);
  testMillis = 65000;
  reader.noteTurn(true, true);
  reader.onRender(ReadingStatsFrame::Page);
  ASSERT_TRUE(reader.finish());
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(cachePath())).totalReadingSeconds, 1234U);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().totalReadingSeconds, 5678U);
}

TEST_F(ClosedCompletionTransaction, FailedReaderInitializationReleasesLease) {
  Storage.failReadsFor("/.crosspoint/global_stats.bin");
  {
    ReaderStats reader;
    reader.begin(cachePath(), BOOK, true, 300);
    EXPECT_TRUE(reader.unavailableAtStart());
    EXPECT_FALSE(StatsSessionLease::anyActive());
  }
  EXPECT_FALSE(StatsSessionLease::anyActive());
}

TEST_F(ClosedCompletionTransaction, PendingCompletionPreventsNewTrackedSession) {
  namespace cj = readingstats::completionjournal;
  ASSERT_TRUE(Storage.mkdir("/.completion-journal"));
  BookReadingStats book;
  GlobalReadingStats global;
  cj::Baseline baseline{};
  ASSERT_TRUE(book.encode(baseline.book.data(), baseline.book.size()));
  ASSERT_TRUE(global.encode(baseline.global));
  baseline.bookSize = baseline.book.size();
  baseline.globalSize = baseline.global.size();
  book.isCompleted = true;
  global.completedBooks = 1;
  cj::Book afterBook{};
  cj::Global afterGlobal{};
  ASSERT_TRUE(book.encode(afterBook.data(), afterBook.size()));
  ASSERT_TRUE(global.encode(afterGlobal));
  cj::Record record{};
  size_t size = 0;
  ASSERT_TRUE(cj::encode(readingstats::bookDirectory(cachePath()), afterBook, afterGlobal, baseline, record, size));
  ASSERT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Created);
  ASSERT_EQ(cj::createSeal(0, record, size), cj::CreateResult::Created);
  ReaderStats reader;
  reader.begin(cachePath(), BOOK, true, 300);
  EXPECT_TRUE(reader.unavailableAtStart());
  EXPECT_FALSE(StatsSessionLease::anyActive());
  EXPECT_TRUE(Storage.exists("/.completion-journal/.completion-00"));
}

TEST_F(ClosedCompletionTransaction, AllLiveReadersMustReleaseBeforeEditing) {
  {
    ReaderStats first;
    first.begin(cachePath(), BOOK, true, 300);
    {
      ReaderStats second;
      second.begin(cachePath(), BOOK, true, 300);
      ASSERT_TRUE(StatsSessionLease::anyActive());
    }
    EXPECT_EQ(readingstats::setClosedBookCompletionStats(BOOK, true, {}), readingstats::ClosedCompletionResult::Busy);
  }
  EXPECT_FALSE(StatsSessionLease::anyActive());
  EXPECT_EQ(readingstats::setClosedBookCompletionStats(BOOK, true, {}), readingstats::ClosedCompletionResult::Applied);
}

TEST_F(ClosedCompletionTransaction, RetryRecoversOnlyTheRequestedBookAndStatus) {
  namespace cj = readingstats::completionjournal;
  using readingstats::CompletionRecoveryResult;
  ASSERT_TRUE(Storage.mkdir("/.completion-journal"));
  BookReadingStats book;
  book.totalReadingSeconds = 1234;
  book.totalPagesTurned = 32;
  GlobalReadingStats global;
  global.totalReadingSeconds = 5678;
  cj::Baseline before{};
  ASSERT_TRUE(book.encode(before.book.data(), before.book.size()));
  ASSERT_TRUE(global.encode(before.global));
  before.bookSize = before.book.size();
  before.globalSize = before.global.size();
  book.isCompleted = true;
  global.completedBooks = 1;
  cj::Book afterBook{};
  cj::Global afterGlobal{};
  ASSERT_TRUE(book.encode(afterBook.data(), afterBook.size()));
  ASSERT_TRUE(global.encode(afterGlobal));
  cj::Record record{};
  size_t size = 0;
  ASSERT_TRUE(cj::encode(readingstats::bookDirectory(cachePath()), afterBook, afterGlobal, before, record, size));
  ASSERT_EQ(cj::start(record, size), cj::StartResult::Started);
  bool target = false;
  EXPECT_EQ(readingstats::probeClosedBookCompletion(BOOK, target), readingstats::CompletionProbeResult::Matching);
  EXPECT_TRUE(target);
  EXPECT_EQ(readingstats::recoverClosedBookCompletionStats(BOOK, false),
            CompletionRecoveryResult::DifferentTransaction);
  HalFile other;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/other.epub", other));
  ASSERT_TRUE(other.close());
  target = false;
  EXPECT_EQ(readingstats::probeClosedBookCompletion("/other.epub", target),
            readingstats::CompletionProbeResult::DifferentTransaction);
  EXPECT_FALSE(target);
  EXPECT_EQ(readingstats::recoverClosedBookCompletionStats("/other.epub", true),
            CompletionRecoveryResult::DifferentTransaction);
  EXPECT_TRUE(Storage.exists("/.completion-journal/.completion-00"));
  EXPECT_EQ(readingstats::recoverClosedBookCompletionStats(BOOK, true), CompletionRecoveryResult::Recovered);
  EXPECT_EQ(readingstats::finalizeClosedBookCompletion(BOOK, false),
            readingstats::CompletionFinalizeResult::DifferentTransaction);
  EXPECT_EQ(readingstats::finalizeClosedBookCompletion(BOOK, true), readingstats::CompletionFinalizeResult::Finalized);
  EXPECT_TRUE(BookReadingStats::load(readingstats::bookDirectory(cachePath())).isCompleted);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().completedBooks, 1u);
  EXPECT_EQ(readingstats::recoverClosedBookCompletionStats(BOOK, true), CompletionRecoveryResult::NoPending);
  target = false;
  EXPECT_EQ(readingstats::probeClosedBookCompletion(BOOK, target), readingstats::CompletionProbeResult::NoPending);
  EXPECT_FALSE(target);
}

TEST_F(ClosedCompletionTransaction, RetryLeavesAmbiguousTemporaryFilesForReview) {
  Storage.failNextRenameFrom("/.crosspoint/global_stats.bin.tmp");
  ASSERT_EQ(readingstats::setClosedBookCompletionStats(BOOK, true, {2026, 11, 10}),
            readingstats::ClosedCompletionResult::RecoveryRequired);
  EXPECT_EQ(readingstats::recoverClosedBookCompletionStats(BOOK, true),
            readingstats::CompletionRecoveryResult::ManualReview);
  EXPECT_TRUE(Storage.exists("/.crosspoint/global_stats.bin.tmp"));
  EXPECT_TRUE(Storage.exists("/.completion-journal/.completion-00"));
}
