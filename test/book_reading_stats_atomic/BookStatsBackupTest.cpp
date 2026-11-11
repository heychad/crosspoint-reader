#include <Arduino.h>
#include <BookReadingStats.h>
#include <BookStatsBackup.h>
#include <CompletionTransaction.h>
#include <GlobalReadingStats.h>
#include <HalStorage.h>
#include <ReadingStatsStore.h>
#include <StatsSessionLease.h>
#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

namespace {
constexpr char BOOK_PATH[] = "/books/example.epub";
const std::string LEAF = "epub_" + std::to_string(std::hash<std::string>{}(BOOK_PATH));
const std::string CACHE = "/.crosspoint/" + LEAF;
const std::string BACKUPS = "/.crossink-stats-backup/books/" + LEAF;
BookReadingStats fixture() {
  BookReadingStats book;
  book.totalReadingSeconds = 1234;
  book.sessionCount = 3;
  book.startDate = {2024, 2, 29};
  book.startDateManual = true;
  return book;
}
std::vector<std::string> names() {
  std::vector<std::string> result;
  auto directory = Storage.open(BACKUPS.c_str());
  if (!directory) return result;
  for (auto entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    char name[128]{};
    entry.getName(name, sizeof(name));
    result.emplace_back(name);
  }
  return result;
}
class BookStatsBackupTest : public testing::Test {
 protected:
  void SetUp() override {
    Storage.reset();
    testMillis = 7;
    ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), fixture()));
    HalFile book;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", BOOK_PATH, book));
    ASSERT_EQ(book.write(uint8_t{1}), 1U);
    ASSERT_TRUE(book.close());
    ASSERT_TRUE(GlobalReadingStats{}.save());
  }
};
}  // namespace

TEST_F(BookStatsBackupTest, CreatesExactVerifiedBookSnapshotWithoutOverwrite) {
  ASSERT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  auto files = names();
  ASSERT_EQ(files.size(), 1U);
  EXPECT_TRUE(files[0].starts_with("book_0000000008_"));
  HalFile file;
  ASSERT_TRUE(Storage.openFileForRead("TEST", std::string(BACKUPS) + "/" + files[0], file));
  std::array<uint8_t, BookReadingStats::CURRENT_FILE_SIZE> actual{}, expected{};
  ASSERT_TRUE(fixture().encode(expected.data(), expected.size()));
  ASSERT_EQ(file.read(actual.data(), actual.size()), static_cast<int>(actual.size()));
  EXPECT_EQ(actual, expected);
  EXPECT_TRUE(file.close());
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, fixture()));
  ++testMillis;
  EXPECT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  EXPECT_EQ(names().size(), 2U);
}

TEST_F(BookStatsBackupTest, ResetKeepsBookStateAndVerifiedPreviousStatistics) {
  for (const auto* name : {"/progress.bin", "/bookmarks.bin", "/reader_settings.bin", "/reading_stats_off"}) {
    HalFile file;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", CACHE + name, file));
    ASSERT_EQ(file.write(uint8_t{42}), 1U);
    ASSERT_TRUE(file.close());
  }
  ASSERT_EQ(statsbackup::resetBookStatistics(CACHE, BOOK_PATH), statsbackup::BookRestoreResult::Restored);
  const auto reset = BookReadingStats::load(readingstats::bookDirectory(CACHE));
  EXPECT_EQ(reset.totalReadingSeconds, 0U);
  EXPECT_EQ(reset.sessionCount, 0U);
  EXPECT_FALSE(reset.startDate.isValid());
  EXPECT_FALSE(reset.startDateManual);
  EXPECT_FALSE(reset.isCompleted);
  ASSERT_EQ(names().size(), 1U);
  BookReadingStats previous;
  ASSERT_TRUE(statsbackup::readBookBackup(CACHE, names().front(), previous));
  EXPECT_EQ(previous.totalReadingSeconds, 1234U);
  EXPECT_TRUE(previous.startDateManual);
  for (const auto* name : {"/progress.bin", "/bookmarks.bin", "/reader_settings.bin", "/reading_stats_off"}) {
    HalFile file;
    ASSERT_TRUE(Storage.openFileForRead("TEST", CACHE + name, file));
    uint8_t value = 0;
    EXPECT_EQ(file.read(&value, 1), 1);
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(file.close());
  }
  EXPECT_EQ(statsbackup::resetBookStatistics(CACHE, BOOK_PATH), statsbackup::BookRestoreResult::Restored);
  EXPECT_EQ(names().size(), 1U);
}

TEST_F(BookStatsBackupTest, ResetCompletedBookAdjustsCountWithoutDeletingDeviceHistory) {
  auto current = fixture();
  current.isCompleted = true;
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), current));
  GlobalReadingStats global;
  global.totalReadingSeconds = 5000;
  global.totalPagesTurned = 120;
  global.completedBooks = 4;
  ASSERT_TRUE(global.save());
  ASSERT_EQ(statsbackup::resetBookStatistics(CACHE, BOOK_PATH), statsbackup::BookRestoreResult::Restored);
  const auto updated = GlobalReadingStats::loadReadOnly();
  EXPECT_EQ(updated.completedBooks, 3U);
  EXPECT_EQ(updated.totalReadingSeconds, 5000U);
  EXPECT_EQ(updated.totalPagesTurned, 120U);
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(CACHE)).totalReadingSeconds, 0U);
}

TEST_F(BookStatsBackupTest, ResetRefusesLiveReaderAndStaleConfirmation) {
  {
    StatsSessionLease lease;
    ASSERT_TRUE(lease.acquire());
    EXPECT_EQ(statsbackup::resetBookStatistics(CACHE, BOOK_PATH), statsbackup::BookRestoreResult::Busy);
  }
  std::array<uint8_t, BookReadingStats::CURRENT_FILE_SIZE> expected{};
  auto stale = fixture();
  ++stale.totalReadingSeconds;
  ASSERT_TRUE(stale.encode(expected.data(), expected.size()));
  EXPECT_EQ(statsbackup::resetBookStatistics(CACHE, BOOK_PATH, expected.data(), expected.size()),
            statsbackup::BookRestoreResult::Unavailable);
  EXPECT_TRUE(names().empty());
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(CACHE)).totalReadingSeconds, 1234U);
}

TEST_F(BookStatsBackupTest, ResetRefusesFailedBackupAndRetainsHistory) {
  const auto bytes = fixture();
  ASSERT_TRUE(statsbackup::createBookBackup(CACHE, bytes));
  // The same timestamp and payload collide with a prior backup; never overwrite it.
  EXPECT_EQ(statsbackup::resetBookStatistics(CACHE, BOOK_PATH), statsbackup::BookRestoreResult::BackupFailed);
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(CACHE)).totalReadingSeconds, 1234U);
  ++testMillis;
  EXPECT_EQ(statsbackup::resetBookStatistics(CACHE, BOOK_PATH), statsbackup::BookRestoreResult::Restored);
}

TEST_F(BookStatsBackupTest, ResetWriteFailureKeepsIndependentBackup) {
  Storage.failNextRenameFrom(readingstats::bookDirectory(CACHE) + "/stats_v5.bin.tmp");
  EXPECT_EQ(statsbackup::resetBookStatistics(CACHE, BOOK_PATH), statsbackup::BookRestoreResult::WriteUncertain);
  ASSERT_EQ(names().size(), 1U);
  BookReadingStats previous;
  ASSERT_TRUE(statsbackup::readBookBackup(CACHE, names().front(), previous));
  EXPECT_EQ(previous.totalReadingSeconds, 1234U);
}

TEST_F(BookStatsBackupTest, ResetSupportsXtcCacheIdentity) {
  const std::string path = "/books/example.xtc";
  const std::string cache = "/.crosspoint/xtc_" + std::to_string(std::hash<std::string>{}(path));
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", path, file));
  ASSERT_TRUE(file.close());
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(cache), fixture()));
  ASSERT_EQ(statsbackup::resetBookStatistics(cache, path), statsbackup::BookRestoreResult::Restored);
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(cache)).totalReadingSeconds, 0U);
}

TEST_F(BookStatsBackupTest, AmbiguousCompletedResetRetainsJournalAndBackupForReview) {
  auto current = fixture();
  current.isCompleted = true;
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), current));
  GlobalReadingStats global;
  global.completedBooks = 1;
  ASSERT_TRUE(global.save());
  Storage.failNextRenameFrom("/.crosspoint/global_stats.bin.tmp");
  ASSERT_EQ(statsbackup::resetBookStatistics(CACHE, BOOK_PATH), statsbackup::BookRestoreResult::RecoveryRequired);
  bool target = true;
  ASSERT_EQ(readingstats::probeClosedBookCompletion(BOOK_PATH, target), readingstats::CompletionProbeResult::Matching);
  EXPECT_FALSE(target);
  ASSERT_EQ(readingstats::recoverClosedBookCompletionStats(BOOK_PATH, false),
            readingstats::CompletionRecoveryResult::ManualReview);
  EXPECT_TRUE(Storage.exists("/.crosspoint/global_stats.bin.tmp"));
  EXPECT_TRUE(Storage.exists("/.completion-journal/.completion-00"));
  ASSERT_EQ(names().size(), 1U);
  // The preserved backup remains byte-for-byte available even while normal
  // statistics reads are fenced by the unresolved journal.
  HalFile backup;
  ASSERT_TRUE(Storage.openFileForRead("TEST", BACKUPS + "/" + names().front(), backup));
  std::array<uint8_t, BookReadingStats::CURRENT_FILE_SIZE> expected{}, actual{};
  ASSERT_TRUE(current.encode(expected.data(), expected.size()));
  ASSERT_EQ(backup.read(actual.data(), actual.size()), static_cast<int>(actual.size()));
  EXPECT_EQ(actual, expected);
  EXPECT_TRUE(backup.close());
  EXPECT_EQ(statsbackup::resetBookStatistics(CACHE, BOOK_PATH), statsbackup::BookRestoreResult::Unavailable);
}

TEST_F(BookStatsBackupTest, ReadsVerifiedSnapshotForSelectedBook) {
  ASSERT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  const auto name = names().at(0);
  BookReadingStats output;
  ASSERT_TRUE(statsbackup::readBookBackup(CACHE, name, output));
  EXPECT_EQ(output.totalReadingSeconds, 1234U);
  EXPECT_TRUE(output.startDateManual);
  EXPECT_EQ(output.startDate.year, 2024);
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(CACHE)).totalReadingSeconds, 1234U);
}

TEST_F(BookStatsBackupTest, RejectsWrongBookAndCorruptOrUnreadableBackupWithoutChangingOutput) {
  ASSERT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  const auto name = names().at(0);
  const auto path = std::string(BACKUPS) + "/" + name;
  BookReadingStats output;
  output.totalReadingSeconds = 7654;
  EXPECT_FALSE(statsbackup::readBookBackup("/.crosspoint/epub_456", name, output));
  EXPECT_FALSE(statsbackup::readBookBackup(CACHE, "../" + name, output));
  Storage.failNextCloseFor(path);
  EXPECT_FALSE(statsbackup::readBookBackup(CACHE, name, output));
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", path, file));
  std::array<uint8_t, BookReadingStats::CURRENT_FILE_SIZE> data{};
  ASSERT_TRUE(fixture().encode(data.data(), data.size()));
  data[3] ^= 1;
  ASSERT_EQ(file.write(data.data(), data.size()), data.size());
  ASSERT_TRUE(file.close());
  EXPECT_FALSE(statsbackup::readBookBackup(CACHE, name, output));
  EXPECT_EQ(output.totalReadingSeconds, 7654U);
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(CACHE)).totalReadingSeconds, 1234U);
}

TEST_F(BookStatsBackupTest, RestoresBookAndKeepsPreviousSnapshot) {
  ASSERT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  const auto name = names().at(0);
  auto current = fixture();
  current.totalReadingSeconds = 2500;
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), current));
  ++testMillis;
  EXPECT_EQ(statsbackup::restoreBookBackup(CACHE, BOOK_PATH, name), statsbackup::BookRestoreResult::Restored);
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(CACHE)).totalReadingSeconds, 1234U);
  const auto backups = names();
  ASSERT_EQ(backups.size(), 2U);
  bool hasPrevious = false;
  for (const auto& candidate : backups) {
    BookReadingStats decoded;
    ASSERT_TRUE(statsbackup::readBookBackup(CACHE, candidate, decoded));
    hasPrevious |= decoded.totalReadingSeconds == 2500U;
  }
  EXPECT_TRUE(hasPrevious);
}

TEST_F(BookStatsBackupTest, RestoreCoordinatesCompletionAndRefusesLiveReader) {
  ASSERT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  const auto name = names().at(0);
  auto current = fixture();
  current.isCompleted = true;
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), current));
  GlobalReadingStats global;
  global.completedBooks = 1;
  ASSERT_TRUE(global.save());
  ++testMillis;
  {
    StatsSessionLease lease;
    ASSERT_TRUE(lease.acquire());
    EXPECT_EQ(statsbackup::restoreBookBackup(CACHE, BOOK_PATH, name), statsbackup::BookRestoreResult::Busy);
  }
  EXPECT_EQ(statsbackup::restoreBookBackup(CACHE, BOOK_PATH, name), statsbackup::BookRestoreResult::Restored);
  EXPECT_FALSE(BookReadingStats::load(readingstats::bookDirectory(CACHE)).isCompleted);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().completedBooks, 0U);
  EXPECT_EQ(names().size(), 2U);
}

TEST_F(BookStatsBackupTest, RestoringFinishedBookIncrementsDeviceTotal) {
  auto finished = fixture();
  finished.isCompleted = true;
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), finished));
  ASSERT_TRUE(statsbackup::createBookBackup(CACHE, finished));
  const auto name = names().at(0);
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), fixture()));
  ++testMillis;
  EXPECT_EQ(statsbackup::restoreBookBackup(CACHE, BOOK_PATH, name), statsbackup::BookRestoreResult::Restored);
  EXPECT_TRUE(BookReadingStats::load(readingstats::bookDirectory(CACHE)).isCompleted);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().completedBooks, 1U);
}

TEST_F(BookStatsBackupTest, CompletionRestorePreservesInterruptedJournalForReview) {
  auto finished = fixture();
  finished.isCompleted = true;
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), finished));
  ASSERT_TRUE(statsbackup::createBookBackup(CACHE, finished));
  const auto name = names().at(0);
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), fixture()));
  ++testMillis;
  Storage.failNextRenameFrom("/.crosspoint/global_stats.bin.tmp");
  EXPECT_EQ(statsbackup::restoreBookBackup(CACHE, BOOK_PATH, name), statsbackup::BookRestoreResult::RecoveryRequired);
  EXPECT_EQ(readingstats::recoverClosedBookCompletionStats(BOOK_PATH, true),
            readingstats::CompletionRecoveryResult::ManualReview);
  EXPECT_TRUE(Storage.exists("/.crosspoint/global_stats.bin.tmp"));
  EXPECT_TRUE(Storage.exists((BACKUPS + "/" + name).c_str()));
  EXPECT_EQ(names().size(), 2U);
}

TEST_F(BookStatsBackupTest, RestoreRejectsChangedPreviewAndMissingBook) {
  ASSERT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  const auto name = names().at(0);
  std::array<uint8_t, BookReadingStats::CURRENT_FILE_SIZE> preview{};
  ASSERT_TRUE(fixture().encode(preview.data(), preview.size()));
  auto current = fixture();
  current.totalReadingSeconds = 2500;
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), current));
  ++testMillis;
  EXPECT_EQ(statsbackup::restoreBookBackup(CACHE, BOOK_PATH, name, preview.data(), preview.size()),
            statsbackup::BookRestoreResult::Unavailable);
  EXPECT_EQ(statsbackup::restoreBookBackup(CACHE, "/books/missing.epub", name),
            statsbackup::BookRestoreResult::Unavailable);
  EXPECT_EQ(statsbackup::restoreBookBackup("/.crosspoint/epub_456", BOOK_PATH, name),
            statsbackup::BookRestoreResult::Unavailable);
  EXPECT_EQ(BookReadingStats::load(readingstats::bookDirectory(CACHE)).totalReadingSeconds, 2500U);
  EXPECT_EQ(names().size(), 1U);
}

TEST_F(BookStatsBackupTest, FailedRestorePreservesBackupsAndUncertainTemporary) {
  ASSERT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  const auto name = names().at(0);
  auto current = fixture();
  current.totalReadingSeconds = 2500;
  const auto primary = readingstats::bookDirectory(CACHE) + "/stats_v5.bin";
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), current));
  ++testMillis;
  Storage.failNextRenameFrom(primary + ".tmp");
  EXPECT_EQ(statsbackup::restoreBookBackup(CACHE, BOOK_PATH, name), statsbackup::BookRestoreResult::WriteUncertain);
  EXPECT_TRUE(Storage.exists((std::string(BACKUPS) + "/" + name).c_str()));
  EXPECT_TRUE(Storage.exists((primary + ".tmp").c_str()));
  EXPECT_EQ(names().size(), 2U);
}

TEST_F(BookStatsBackupTest, RejectsMissingUnreadableOrChangedSource) {
  auto changed = fixture();
  ++changed.totalReadingSeconds;
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, changed));
  const auto path = readingstats::bookDirectory(CACHE) + "/stats_v5.bin";
  Storage.failReadsFor(path);
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, fixture()));
  Storage.reset();
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, fixture()));
  EXPECT_TRUE(names().empty());
}

TEST_F(BookStatsBackupTest, RejectsUnsafeIdentityAndSourceCloseFailure) {
  EXPECT_FALSE(statsbackup::createBookBackup("/.crosspoint/..", fixture()));
  EXPECT_FALSE(statsbackup::createBookBackup("/.crosspoint/", fixture()));
  Storage.failNextCloseFor(readingstats::bookDirectory(CACHE) + "/stats_v5.bin");
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, fixture()));
  EXPECT_TRUE(names().empty());
}

TEST_F(BookStatsBackupTest, FailedWritePreservesIncompleteTargetAndSource) {
  ASSERT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  const auto name = names().at(0);
  Storage.reset();
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), fixture()));
  const std::string target = std::string(BACKUPS) + "/" + name;
  Storage.failNextWriteFor(target, 10);
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, fixture()));
  HalFile incomplete;
  ASSERT_TRUE(Storage.openFileForRead("TEST", target, incomplete));
  EXPECT_EQ(incomplete.fileSize(), 10U);
  EXPECT_TRUE(incomplete.close());
  EXPECT_TRUE(Storage.exists((readingstats::bookDirectory(CACHE) + "/stats_v5.bin").c_str()));
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, fixture()));
  ++testMillis;
  EXPECT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  ASSERT_TRUE(Storage.openFileForRead("TEST", target, incomplete));
  EXPECT_EQ(incomplete.fileSize(), 10U);
  EXPECT_TRUE(incomplete.close());
  EXPECT_EQ(names().size(), 2U);
}

TEST_F(BookStatsBackupTest, ExistingLegacyPartialIsPreservedAndBlocksSameName) {
  ASSERT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  const auto name = names().at(0);
  Storage.reset();
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), fixture()));
  const std::string target = std::string(BACKUPS) + "/" + name;
  const std::string partial = target + ".part";
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", partial, file));
  ASSERT_EQ(file.write(uint8_t{42}), 1U);
  ASSERT_TRUE(file.close());
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, fixture()));
  EXPECT_TRUE(Storage.exists(partial.c_str()));
  EXPECT_FALSE(Storage.exists(target.c_str()));
}

TEST_F(BookStatsBackupTest, FailedTargetClosePreservesFileAndPreventsOverwrite) {
  ASSERT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  const auto name = names().at(0);
  Storage.reset();
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), fixture()));
  const std::string target = std::string(BACKUPS) + "/" + name;
  Storage.failNextNewCloseFor(target);
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, fixture()));
  EXPECT_TRUE(Storage.exists(target.c_str()));
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, fixture()));
  ++testMillis;
  EXPECT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  EXPECT_EQ(names().size(), 2U);
}

TEST_F(BookStatsBackupTest, PaceResetPreservesHistoryCompletionAndDeviceTotals) {
  auto prior = fixture();
  prior.totalPagesTurned = 57;
  prior.isCompleted = true;
  prior.finishedDate = {2024, 3, 5};
  prior.finishedDateManual = true;
  prior.avgSecondsPerForwardPage = 18;
  prior.paceSampleCount = 9;
  prior.estimatedTimeLeftSeconds = 1800;
  prior.timeOfDaySeconds.fill(20);
  prior.dayOfWeekSeconds.fill(30);
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), prior));
  GlobalReadingStats device;
  device.completedBooks = 7;
  device.totalReadingSeconds = 123456;
  device.totalPagesTurned = 432;
  ASSERT_TRUE(device.save());
  std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> deviceBefore{}, deviceAfter{};
  ASSERT_TRUE(device.encode(deviceBefore));
  ASSERT_EQ(statsbackup::resetBookStatistics(CACHE, BOOK_PATH, nullptr, 0, statsbackup::BookResetScope::ReadingPace),
            statsbackup::BookRestoreResult::Restored);
  auto expected = prior;
  expected.avgSecondsPerForwardPage = 0;
  expected.paceSampleCount = 0;
  expected.estimatedTimeLeftSeconds = 0;
  std::array<uint8_t, BookReadingStats::CURRENT_FILE_SIZE> expectedBytes{}, actualBytes{}, priorBytes{};
  ASSERT_TRUE(expected.encode(expectedBytes.data(), expectedBytes.size()));
  ASSERT_TRUE(
      BookReadingStats::load(readingstats::bookDirectory(CACHE)).encode(actualBytes.data(), actualBytes.size()));
  EXPECT_EQ(actualBytes, expectedBytes);
  ASSERT_TRUE(GlobalReadingStats::loadReadOnly().encode(deviceAfter));
  EXPECT_EQ(deviceAfter, deviceBefore);
  ASSERT_EQ(names().size(), 1U);
  BookReadingStats backup;
  ASSERT_TRUE(statsbackup::readBookBackup(CACHE, names().front(), backup));
  ASSERT_TRUE(backup.encode(actualBytes.data(), actualBytes.size()));
  ASSERT_TRUE(prior.encode(priorBytes.data(), priorBytes.size()));
  EXPECT_EQ(actualBytes, priorBytes);
  ASSERT_EQ(statsbackup::resetBookStatistics(CACHE, BOOK_PATH, nullptr, 0, statsbackup::BookResetScope::ReadingPace),
            statsbackup::BookRestoreResult::Restored);
  EXPECT_EQ(names().size(), 1U);
}

TEST_F(BookStatsBackupTest, PaceResetFailureRetainsPreviousSnapshot) {
  auto prior = fixture();
  prior.avgSecondsPerForwardPage = 18;
  prior.paceSampleCount = 9;
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), prior));
  Storage.failNextRenameFrom(readingstats::bookDirectory(CACHE) + "/stats_v5.bin.tmp");
  EXPECT_EQ(statsbackup::resetBookStatistics(CACHE, BOOK_PATH, nullptr, 0, statsbackup::BookResetScope::ReadingPace),
            statsbackup::BookRestoreResult::WriteUncertain);
  ASSERT_EQ(names().size(), 1U);
  BookReadingStats backup;
  ASSERT_TRUE(statsbackup::readBookBackup(CACHE, names().front(), backup));
  EXPECT_EQ(backup.avgSecondsPerForwardPage, 18U);
  EXPECT_EQ(backup.totalReadingSeconds, prior.totalReadingSeconds);
}
