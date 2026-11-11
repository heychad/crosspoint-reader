#include <BookReadingStats.h>
#include <BookStatsBackup.h>
#include <BookTracking.h>
#include <CompletionJournalIO.h>
#include <DeviceStatsBackup.h>
#include <GlobalReadingStats.h>
#include <HalStorage.h>
#include <ReadingStatsStore.h>
#include <gtest/gtest.h>

namespace cj = readingstats::completionjournal;
namespace {
constexpr char DIRECTORY[] = "/.crosspoint/reading_stats/epub_123";
constexpr char BOOK[] = "/.crosspoint/reading_stats/epub_123/stats_v5.bin";
constexpr char GLOBAL[] = "/.crosspoint/global_stats.bin";
void put(const char* path, const uint8_t* bytes, size_t size) {
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", path, file));
  ASSERT_EQ(file.write(bytes, size), size);
  ASSERT_TRUE(file.close());
}
class CompletionReplayCheck : public testing::Test {
 protected:
  cj::Baseline baseline{};
  cj::Book nextBook{};
  cj::Global nextGlobal{};
  cj::Record record{};
  size_t size = 0;
  void SetUp() override {
    Storage.reset();
    BookReadingStats oldBook;
    GlobalReadingStats oldGlobal;
    ASSERT_TRUE(oldBook.encode(baseline.book.data(), baseline.book.size()));
    ASSERT_TRUE(oldGlobal.encode(baseline.global));
    baseline.bookSize = baseline.book.size();
    baseline.globalSize = baseline.global.size();
    put(BOOK, baseline.book.data(), baseline.bookSize);
    put(GLOBAL, baseline.global.data(), baseline.globalSize);
    oldBook.isCompleted = true;
    oldGlobal.completedBooks = 1;
    ASSERT_TRUE(oldBook.encode(nextBook.data(), nextBook.size()));
    ASSERT_TRUE(oldGlobal.encode(nextGlobal));
    ASSERT_TRUE(cj::encode(DIRECTORY, nextBook, nextGlobal, baseline, record, size));
    ASSERT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Created);
    ASSERT_EQ(cj::createSeal(0, record, size), cj::CreateResult::Created);
  }
};
}  // namespace

TEST_F(CompletionReplayCheck, AcceptsOnlyPreviousOrIntendedSnapshots) {
  cj::SnapshotState book = cj::SnapshotState::After, global = cj::SnapshotState::After;
  EXPECT_EQ(cj::checkReplay(book, global), cj::ReplayCheck::Ready);
  EXPECT_EQ(book, cj::SnapshotState::Before);
  EXPECT_EQ(global, cj::SnapshotState::Before);
  put(BOOK, nextBook.data(), nextBook.size());
  EXPECT_EQ(cj::checkReplay(book, global), cj::ReplayCheck::Ready);
  EXPECT_EQ(book, cj::SnapshotState::After);
  EXPECT_EQ(global, cj::SnapshotState::Before);
  put(GLOBAL, nextGlobal.data(), nextGlobal.size());
  EXPECT_EQ(cj::checkReplay(book, global), cj::ReplayCheck::Ready);
  EXPECT_EQ(global, cj::SnapshotState::After);
  auto later = nextGlobal;
  later[5] = 99;
  put(GLOBAL, later.data(), later.size());
  book = global = cj::SnapshotState::Before;
  EXPECT_EQ(cj::checkReplay(book, global), cj::ReplayCheck::Conflict);
  EXPECT_EQ(book, cj::SnapshotState::Before);
  EXPECT_EQ(global, cj::SnapshotState::Before);
}

TEST_F(CompletionReplayCheck, PreservesUncertainTemporaryFilesAndReadFailures) {
  cj::SnapshotState book = cj::SnapshotState::After, global = cj::SnapshotState::After;
  put("/.crosspoint/global_stats.bin.tmp", nextGlobal.data(), nextGlobal.size());
  EXPECT_EQ(cj::checkReplay(book, global), cj::ReplayCheck::Conflict);
  EXPECT_TRUE(Storage.exists("/.crosspoint/global_stats.bin.tmp"));
  ASSERT_TRUE(Storage.remove("/.crosspoint/global_stats.bin.tmp"));
  Storage.failNextReadFor(BOOK);
  EXPECT_EQ(cj::checkReplay(book, global), cj::ReplayCheck::Unavailable);
  EXPECT_EQ(book, cj::SnapshotState::After);
  EXPECT_EQ(global, cj::SnapshotState::After);
  EXPECT_TRUE(Storage.exists(BOOK));
}

TEST(CompletionReplayAbsent, MissingBaselinesDoNotAuthorizeUnexpectedFiles) {
  Storage.reset();
  BookReadingStats book;
  GlobalReadingStats global;
  cj::Book nextBook{};
  cj::Global nextGlobal{};
  ASSERT_TRUE(book.encode(nextBook.data(), nextBook.size()));
  ASSERT_TRUE(global.encode(nextGlobal));
  cj::Record record{};
  size_t size = 0;
  ASSERT_TRUE(cj::encode(DIRECTORY, nextBook, nextGlobal, {}, record, size));
  ASSERT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Created);
  ASSERT_EQ(cj::createSeal(0, record, size), cj::CreateResult::Created);
  cj::SnapshotState bookState = cj::SnapshotState::After, globalState = cj::SnapshotState::After;
  EXPECT_EQ(cj::checkReplay(bookState, globalState), cj::ReplayCheck::Ready);
  EXPECT_EQ(bookState, cj::SnapshotState::Before);
  EXPECT_EQ(globalState, cj::SnapshotState::Before);
  auto unexpected = nextBook;
  unexpected[1] = 1;
  put(BOOK, unexpected.data(), unexpected.size());
  EXPECT_EQ(cj::checkReplay(bookState, globalState), cj::ReplayCheck::Conflict);
  EXPECT_TRUE(Storage.exists(BOOK));
}

TEST_F(CompletionReplayCheck, AppliesBothSnapshotsAndRetainsJournal) {
  EXPECT_EQ(cj::replay(), cj::ReplayResult::AppliedUnretired);
  EXPECT_TRUE(Storage.exists("/.completion-journal/.completion-00"));
  EXPECT_TRUE(Storage.exists("/.completion-journal/.cseal-00"));
  cj::SnapshotState book = cj::SnapshotState::Before, global = cj::SnapshotState::Before;
  EXPECT_EQ(cj::checkReplay(book, global), cj::ReplayCheck::Ready);
  EXPECT_EQ(book, cj::SnapshotState::After);
  EXPECT_EQ(global, cj::SnapshotState::After);
  EXPECT_EQ(cj::replay(), cj::ReplayResult::AppliedUnretired);
}

TEST_F(CompletionReplayCheck, FailedSecondSavePreservesJournalAndTemporaryFile) {
  Storage.failNextRenameFrom("/.crosspoint/global_stats.bin.tmp");
  EXPECT_EQ(cj::replay(), cj::ReplayResult::Incomplete);
  EXPECT_TRUE(Storage.exists("/.completion-journal/.completion-00"));
  EXPECT_TRUE(Storage.exists("/.completion-journal/.cseal-00"));
  EXPECT_TRUE(Storage.exists("/.crosspoint/global_stats.bin.tmp"));
  EXPECT_EQ(cj::replay(), cj::ReplayResult::Conflict);
  EXPECT_TRUE(Storage.exists("/.crosspoint/global_stats.bin.tmp"));
}

TEST_F(CompletionReplayCheck, RefusesToOverwriteNewerStatistics) {
  auto newer = nextGlobal;
  newer[5] = 99;
  put(GLOBAL, newer.data(), newer.size());
  EXPECT_EQ(cj::replay(), cj::ReplayResult::Conflict);
  HalFile file;
  ASSERT_TRUE(Storage.openFileForRead("TEST", GLOBAL, file));
  cj::Global actual{};
  ASSERT_EQ(file.read(actual.data(), actual.size()), static_cast<int>(actual.size()));
  EXPECT_TRUE(file.close());
  EXPECT_EQ(actual, newer);
}

TEST_F(CompletionReplayCheck, RefusesAliasedBackupBeforeWriting) {
  ASSERT_TRUE(Storage.aliasPath(BOOK, std::string(BOOK) + ".bak"));
  EXPECT_EQ(cj::replay(), cj::ReplayResult::Incomplete);
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_TRUE(Storage.exists((std::string(BOOK) + ".bak").c_str()));
  EXPECT_FALSE(Storage.exists((std::string(BOOK) + ".tmp").c_str()));
}

TEST(CompletionReplayAbsent, ReplayBlocksUnverifiedLegacySource) {
  Storage.reset();
  BookReadingStats book;
  GlobalReadingStats global;
  cj::Book nextBook{};
  cj::Global nextGlobal{};
  ASSERT_TRUE(book.encode(nextBook.data(), nextBook.size()));
  ASSERT_TRUE(global.encode(nextGlobal));
  cj::Record record{};
  size_t size = 0;
  ASSERT_TRUE(cj::encode(DIRECTORY, nextBook, nextGlobal, {}, record, size));
  ASSERT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Created);
  ASSERT_EQ(cj::createSeal(0, record, size), cj::CreateResult::Created);
  EXPECT_EQ(cj::replay(), cj::ReplayResult::Blocked);
  EXPECT_FALSE(Storage.exists(BOOK));
  EXPECT_FALSE(Storage.exists(GLOBAL));
}

TEST_F(CompletionReplayCheck, RetiresOnlyAfterBothSnapshotsAreApplied) {
  EXPECT_EQ(cj::retire(), cj::RetirementResult::NotApplied);
  EXPECT_FALSE(Storage.exists("/.completion-journal/.cretire-00"));
  ASSERT_EQ(cj::replay(), cj::ReplayResult::AppliedUnretired);
  EXPECT_EQ(cj::retire(), cj::RetirementResult::Retired);
  EXPECT_TRUE(Storage.exists("/.completion-journal/.cretire-00"));
  EXPECT_TRUE(Storage.exists("/.completion-journal/.completion-00"));
  EXPECT_TRUE(Storage.exists("/.completion-journal/.cseal-00"));
  EXPECT_EQ(cj::replay(), cj::ReplayResult::NoCandidate);
  EXPECT_EQ(cj::retire(), cj::RetirementResult::NoCandidate);
  auto later = nextGlobal;
  later[5] = 99;
  put(GLOBAL, later.data(), later.size());
  EXPECT_EQ(cj::replay(), cj::ReplayResult::NoCandidate);
}

TEST_F(CompletionReplayCheck, InterruptedRetirementNeverDeletesJournalOrRetriesOverMarker) {
  ASSERT_EQ(cj::replay(), cj::ReplayResult::AppliedUnretired);
  Storage.failNextWriteFor("/.completion-journal/.cretire-00", 3);
  EXPECT_EQ(cj::retire(), cj::RetirementResult::Uncertain);
  EXPECT_TRUE(Storage.exists("/.completion-journal/.cretire-00"));
  EXPECT_TRUE(Storage.exists("/.completion-journal/.completion-00"));
  EXPECT_TRUE(Storage.exists("/.completion-journal/.cseal-00"));
  EXPECT_EQ(cj::replay(), cj::ReplayResult::Blocked);
  EXPECT_EQ(cj::retire(), cj::RetirementResult::Blocked);
}

TEST_F(CompletionReplayCheck, RechecksReceiptAfterTransientVerificationOpenFailure) {
  ASSERT_EQ(cj::replay(), cj::ReplayResult::AppliedUnretired);
  Storage.failNextOpenReadFor("/.completion-journal/.cretire-00");
  EXPECT_EQ(cj::retire(), cj::RetirementResult::Retired);
  EXPECT_EQ(cj::replay(), cj::ReplayResult::NoCandidate);
}

TEST_F(CompletionReplayCheck, FailedReceiptSyncIsNotPromotedByAReadableReceipt) {
  ASSERT_EQ(cj::replay(), cj::ReplayResult::AppliedUnretired);
  Storage.failNextNewSyncFor("/.completion-journal/.cretire-00");
  EXPECT_EQ(cj::retire(), cj::RetirementResult::Uncertain);
}

TEST_F(CompletionReplayCheck, UnreadableReceiptRemainsUncertain) {
  ASSERT_EQ(cj::replay(), cj::ReplayResult::AppliedUnretired);
  Storage.failNextOpenReadFor("/.completion-journal/.cretire-00", 2);
  EXPECT_EQ(cj::retire(), cj::RetirementResult::Uncertain);
}

TEST_F(CompletionReplayCheck, RetirementRefusesChangedStatistics) {
  ASSERT_EQ(cj::replay(), cj::ReplayResult::AppliedUnretired);
  auto later = nextGlobal;
  later[5] = 99;
  put(GLOBAL, later.data(), later.size());
  EXPECT_EQ(cj::retire(), cj::RetirementResult::Conflict);
  EXPECT_FALSE(Storage.exists("/.completion-journal/.cretire-00"));
  EXPECT_TRUE(Storage.exists("/.completion-journal/.completion-00"));
}

namespace {
class CompletionStart : public testing::Test {
 protected:
  cj::Baseline before{};
  cj::Book afterBook{};
  cj::Global afterGlobal{};
  cj::Record record{};
  size_t size = 0;
  void SetUp() override {
    Storage.reset();
    ASSERT_TRUE(Storage.mkdir("/.completion-journal"));
    BookReadingStats book;
    GlobalReadingStats global;
    ASSERT_TRUE(book.encode(before.book.data(), before.book.size()));
    ASSERT_TRUE(global.encode(before.global));
    before.bookSize = before.book.size();
    before.globalSize = before.global.size();
    put(BOOK, before.book.data(), before.bookSize);
    put(GLOBAL, before.global.data(), before.globalSize);
    book.isCompleted = true;
    global.completedBooks = 1;
    ASSERT_TRUE(book.encode(afterBook.data(), afterBook.size()));
    ASSERT_TRUE(global.encode(afterGlobal));
    ASSERT_TRUE(cj::encode(DIRECTORY, afterBook, afterGlobal, before, record, size));
  }
};
}  // namespace

TEST_F(CompletionStart, StartsOnlyFromBaselineAndRequiresRecoveryUntilRetired) {
  EXPECT_EQ(cj::start(record, size), cj::StartResult::Started);
  EXPECT_EQ(cj::start(record, size), cj::StartResult::NeedsRecovery);
  ASSERT_EQ(cj::replay(), cj::ReplayResult::AppliedUnretired);
  ASSERT_EQ(cj::retire(), cj::RetirementResult::Retired);
  EXPECT_EQ(cj::start(record, size), cj::StartResult::Conflict);
  EXPECT_TRUE(Storage.exists("/.completion-journal/.completion-00"));
  EXPECT_FALSE(Storage.exists("/.completion-journal/.completion-01"));
}

TEST_F(CompletionStart, ChangedBaselineAndTemporaryFilesPreventAnyJournalWrite) {
  auto changed = before.global;
  changed[5] = 1;
  put(GLOBAL, changed.data(), changed.size());
  EXPECT_EQ(cj::start(record, size), cj::StartResult::Conflict);
  EXPECT_FALSE(Storage.exists("/.completion-journal/.completion-00"));
  put(GLOBAL, before.global.data(), before.globalSize);
  put("/.crosspoint/global_stats.bin.tmp", afterGlobal.data(), afterGlobal.size());
  EXPECT_EQ(cj::start(record, size), cj::StartResult::Conflict);
  EXPECT_FALSE(Storage.exists("/.completion-journal/.completion-00"));
}

TEST_F(CompletionStart, PartialSealIsPreservedAndBlocksFurtherStarts) {
  Storage.failNextWriteFor("/.completion-journal/.cseal-00", 3);
  EXPECT_EQ(cj::start(record, size), cj::StartResult::Uncertain);
  EXPECT_TRUE(Storage.exists("/.completion-journal/.completion-00"));
  EXPECT_TRUE(Storage.exists("/.completion-journal/.cseal-00"));
  EXPECT_EQ(cj::start(record, size), cj::StartResult::Blocked);
  EXPECT_FALSE(Storage.exists("/.completion-journal/.completion-01"));
}

TEST_F(CompletionReplayCheck, PendingTransactionBlocksOrdinaryStatsReadsWritesAndDeletes) {
  const auto directory = std::string(DIRECTORY);
  EXPECT_FALSE(BookReadingStats::load(directory).writable);
  EXPECT_FALSE(GlobalReadingStats::load().writable);
  EXPECT_FALSE(GlobalReadingStats::loadReadOnly().writable);
  BookReadingStats book;
  GlobalReadingStats global;
  EXPECT_FALSE(book.save(directory));
  EXPECT_FALSE(global.save());
  EXPECT_FALSE(BookReadingStats::remove(directory));
  EXPECT_FALSE(statsbackup::createBookBackup("/.crosspoint/epub_123", book));
  EXPECT_FALSE(statsbackup::createDeviceBackup(global));
  EXPECT_FALSE(booktracking::setEnabled("/.crosspoint/epub_123", "/book.epub", false));
  EXPECT_FALSE(Storage.exists("/.crosspoint/reading_stats/epub_123/reading_stats_off"));
  {
    readingstats::BookMove move("/.crosspoint/epub_123", "/.crosspoint/epub_456", "/book.epub");
    EXPECT_FALSE(move.prepare());
  }
  EXPECT_TRUE(Storage.exists(BOOK));
  EXPECT_TRUE(Storage.exists(GLOBAL));
  ASSERT_EQ(cj::replay(), cj::ReplayResult::AppliedUnretired);
  EXPECT_FALSE(BookReadingStats::load(directory).writable);
  EXPECT_FALSE(GlobalReadingStats::loadReadOnly().writable);
  ASSERT_EQ(cj::retire(), cj::RetirementResult::Retired);
  EXPECT_TRUE(BookReadingStats::load(directory).writable);
  EXPECT_TRUE(GlobalReadingStats::loadReadOnly().writable);
}
