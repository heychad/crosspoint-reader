#include <BookReadingStats.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <string>

namespace {
constexpr char CACHE_PATH[] = "/cache/book";
const std::string statsPath = std::string(CACHE_PATH) + "/stats_v5.bin";
const std::string previousStatsPath = std::string(CACHE_PATH) + "/stats_v4.bin";

BookReadingStats statsWithSeconds(const uint32_t seconds) {
  BookReadingStats stats;
  stats.totalReadingSeconds = seconds;
  return stats;
}
}  // namespace

class BookReadingStatsAtomicTest : public testing::Test {
 protected:
  void SetUp() override { Storage.reset(); }
};

TEST_F(BookReadingStatsAtomicTest, DecodeChecksPayloadAndPreservesOutputOnFailure) {
  BookReadingStats original;
  original.totalReadingSeconds = 1234;
  original.isCompleted = true;
  original.dayOfWeekSeconds[2] = 55;
  std::array<uint8_t, BookReadingStats::CURRENT_FILE_SIZE> bytes{};
  ASSERT_TRUE(original.encode(bytes.data(), bytes.size()));
  BookReadingStats decoded;
  ASSERT_TRUE(BookReadingStats::decode(bytes.data(), bytes.size(), decoded));
  EXPECT_EQ(decoded.totalReadingSeconds, 1234U);
  EXPECT_TRUE(decoded.isCompleted);
  EXPECT_EQ(decoded.dayOfWeekSeconds[2], 55U);
  EXPECT_FALSE(BookReadingStats::decode(nullptr, bytes.size(), decoded));
  EXPECT_FALSE(BookReadingStats::decode(bytes.data(), bytes.size() - 1, decoded));
  bytes[0] = BookReadingStats::CURRENT_FILE_VERSION + 1;
  EXPECT_FALSE(BookReadingStats::decode(bytes.data(), bytes.size(), decoded));
  EXPECT_EQ(decoded.totalReadingSeconds, 1234U);
  EXPECT_TRUE(decoded.isCompleted);
}

TEST_F(BookReadingStatsAtomicTest, DecodeLegacyClearsFieldsNotPresentInOldPayload) {
  const std::array<uint8_t, 11> bytes{1, 2, 0, 100, 0, 0, 0, 5, 0, 0, 0};
  BookReadingStats decoded;
  decoded.isCompleted = true;
  decoded.dayOfWeekSeconds[2] = 55;
  ASSERT_TRUE(BookReadingStats::decode(bytes.data(), bytes.size(), decoded));
  EXPECT_EQ(decoded.sessionCount, 2U);
  EXPECT_EQ(decoded.totalReadingSeconds, 100U);
  EXPECT_FALSE(decoded.isCompleted);
  EXPECT_EQ(decoded.dayOfWeekSeconds[2], 0U);
}

TEST_F(BookReadingStatsAtomicTest, RestoresOriginalWhenReplacementRenameFails) {
  statsWithSeconds(123).save(CACHE_PATH);
  Storage.failNextRenameFrom(statsPath + ".tmp");

  EXPECT_FALSE(statsWithSeconds(999).save(CACHE_PATH));

  EXPECT_EQ(BookReadingStats::load(CACHE_PATH).totalReadingSeconds, 123U);
  EXPECT_TRUE(Storage.exists(statsPath.c_str()));
  EXPECT_TRUE(Storage.exists((statsPath + ".tmp").c_str()));
  EXPECT_FALSE(Storage.exists((statsPath + ".bak").c_str()));
}

TEST_F(BookReadingStatsAtomicTest, RecoversBackupAfterInterruptedReplacement) {
  statsWithSeconds(456).save(CACHE_PATH);
  ASSERT_TRUE(Storage.rename(statsPath.c_str(), (statsPath + ".bak").c_str()));

  EXPECT_EQ(BookReadingStats::load(CACHE_PATH).totalReadingSeconds, 456U);
  EXPECT_TRUE(Storage.exists(statsPath.c_str()));
  EXPECT_FALSE(Storage.exists((statsPath + ".bak").c_str()));
}

TEST_F(BookReadingStatsAtomicTest, PrefersCompleteTempFileOverOlderBackup) {
  statsWithSeconds(123).save(CACHE_PATH);
  ASSERT_TRUE(Storage.rename(statsPath.c_str(), (statsPath + ".bak").c_str()));
  statsWithSeconds(456).save(CACHE_PATH);
  ASSERT_TRUE(Storage.rename(statsPath.c_str(), (statsPath + ".tmp").c_str()));

  EXPECT_EQ(BookReadingStats::load(CACHE_PATH).totalReadingSeconds, 456U);
  EXPECT_TRUE(Storage.exists(statsPath.c_str()));
  EXPECT_FALSE(Storage.exists((statsPath + ".tmp").c_str()));
}

TEST_F(BookReadingStatsAtomicTest, ReadOnlyLoadReadsRecoveryWithoutPromotingIt) {
  ASSERT_TRUE(statsWithSeconds(123).save(CACHE_PATH));
  ASSERT_TRUE(Storage.rename(statsPath.c_str(), (statsPath + ".bak").c_str()));
  ASSERT_TRUE(statsWithSeconds(456).save(CACHE_PATH));
  ASSERT_TRUE(Storage.rename(statsPath.c_str(), (statsPath + ".tmp").c_str()));
  EXPECT_EQ(BookReadingStats::load(CACHE_PATH, BookReadingStats::LoadMode::ReadOnly).totalReadingSeconds, 456U);
  EXPECT_FALSE(Storage.exists(statsPath.c_str()));
  EXPECT_TRUE(Storage.exists((statsPath + ".tmp").c_str()));
  EXPECT_TRUE(Storage.exists((statsPath + ".bak").c_str()));
  EXPECT_EQ(BookReadingStats::load(CACHE_PATH).totalReadingSeconds, 456U);
  EXPECT_TRUE(Storage.exists(statsPath.c_str()));
}

TEST_F(BookReadingStatsAtomicTest, ReadOnlyLoadFallsBackToBackupWithoutPromotingIt) {
  ASSERT_TRUE(statsWithSeconds(123).save(CACHE_PATH));
  ASSERT_TRUE(Storage.rename(statsPath.c_str(), (statsPath + ".bak").c_str()));
  FsFile temp;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", statsPath + ".tmp", temp));
  ASSERT_EQ(temp.write(static_cast<uint8_t>(5)), 1U);
  ASSERT_TRUE(temp.close());
  EXPECT_EQ(BookReadingStats::load(CACHE_PATH, BookReadingStats::LoadMode::ReadOnly).totalReadingSeconds, 123U);
  EXPECT_FALSE(Storage.exists(statsPath.c_str()));
  EXPECT_TRUE(Storage.exists((statsPath + ".tmp").c_str()));
  EXPECT_TRUE(Storage.exists((statsPath + ".bak").c_str()));
}

TEST_F(BookReadingStatsAtomicTest, IgnoresPartialTempFileAndRecoversBackup) {
  statsWithSeconds(123).save(CACHE_PATH);
  ASSERT_TRUE(Storage.rename(statsPath.c_str(), (statsPath + ".bak").c_str()));
  FsFile tempFile;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", statsPath + ".tmp", tempFile));
  ASSERT_EQ(tempFile.write(static_cast<uint8_t>(5)), 1U);
  ASSERT_TRUE(tempFile.close());

  const auto recovered = BookReadingStats::load(CACHE_PATH);
  EXPECT_EQ(recovered.totalReadingSeconds, 123U);
  EXPECT_FALSE(recovered.writable);
  EXPECT_TRUE(Storage.exists(statsPath.c_str()));
  EXPECT_TRUE(Storage.exists((statsPath + ".tmp").c_str()));
  EXPECT_FALSE(Storage.exists((statsPath + ".bak").c_str()));
}

TEST_F(BookReadingStatsAtomicTest, RecoversTempWhenPublishAndBackupRestoreFail) {
  statsWithSeconds(123).save(CACHE_PATH);
  Storage.failNextRenameFrom(statsPath + ".tmp");
  Storage.failNextRenameFrom(statsPath + ".bak");

  statsWithSeconds(999).save(CACHE_PATH);

  EXPECT_FALSE(Storage.exists(statsPath.c_str()));
  EXPECT_TRUE(Storage.exists((statsPath + ".tmp").c_str()));
  EXPECT_TRUE(Storage.exists((statsPath + ".bak").c_str()));
  EXPECT_EQ(BookReadingStats::load(CACHE_PATH).totalReadingSeconds, 999U);
  EXPECT_TRUE(Storage.exists(statsPath.c_str()));
  EXPECT_FALSE(Storage.exists((statsPath + ".tmp").c_str()));
}

TEST_F(BookReadingStatsAtomicTest, SuccessfulReplacementRemovesTransactionFiles) {
  statsWithSeconds(123).save(CACHE_PATH);
  EXPECT_TRUE(statsWithSeconds(789).save(CACHE_PATH));

  EXPECT_EQ(BookReadingStats::load(CACHE_PATH).totalReadingSeconds, 789U);
  EXPECT_FALSE(Storage.exists((statsPath + ".tmp").c_str()));
  EXPECT_FALSE(Storage.exists((statsPath + ".bak").c_str()));
}

TEST_F(BookReadingStatsAtomicTest, IdenticalSaveSkipsWrite) {
  statsWithSeconds(123).save(CACHE_PATH);
  // The write path always clears a stale backup, so a surviving marker proves it did not run.
  FsFile marker;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", statsPath + ".bak", marker));
  ASSERT_EQ(marker.write(static_cast<uint8_t>(7)), 1U);
  ASSERT_TRUE(marker.close());

  statsWithSeconds(123).save(CACHE_PATH);
  EXPECT_TRUE(Storage.exists((statsPath + ".bak").c_str()));

  statsWithSeconds(124).save(CACHE_PATH);
  EXPECT_FALSE(Storage.exists((statsPath + ".bak").c_str()));
  EXPECT_EQ(BookReadingStats::load(CACHE_PATH).totalReadingSeconds, 124U);
}

TEST_F(BookReadingStatsAtomicTest, RemoveClearsRecoverableTransactionFiles) {
  statsWithSeconds(123).save(CACHE_PATH);
  ASSERT_TRUE(Storage.rename(statsPath.c_str(), (statsPath + ".bak").c_str()));

  EXPECT_TRUE(BookReadingStats::remove(CACHE_PATH));
  EXPECT_FALSE(Storage.exists(statsPath.c_str()));
  EXPECT_FALSE(Storage.exists((statsPath + ".tmp").c_str()));
  EXPECT_FALSE(Storage.exists((statsPath + ".bak").c_str()));
}

TEST_F(BookReadingStatsAtomicTest, FailedBackupRemovalKeepsCanonicalStats) {
  statsWithSeconds(123).save(CACHE_PATH);
  ASSERT_TRUE(Storage.rename(statsPath.c_str(), (statsPath + ".bak").c_str()));
  statsWithSeconds(456).save(CACHE_PATH);
  ASSERT_TRUE(Storage.exists(statsPath.c_str()));
  ASSERT_TRUE(Storage.exists((statsPath + ".bak").c_str()));
  Storage.failNextRemove(statsPath + ".bak");

  EXPECT_FALSE(BookReadingStats::remove(CACHE_PATH));

  EXPECT_TRUE(Storage.exists(statsPath.c_str()));
  EXPECT_EQ(BookReadingStats::load(CACHE_PATH).totalReadingSeconds, 456U);
}

TEST_F(BookReadingStatsAtomicTest, FailedFallbackRemovalKeepsCanonicalStats) {
  statsWithSeconds(123).save(CACHE_PATH);
  ASSERT_TRUE(Storage.rename(statsPath.c_str(), previousStatsPath.c_str()));
  statsWithSeconds(456).save(CACHE_PATH);
  ASSERT_TRUE(Storage.exists(statsPath.c_str()));
  ASSERT_TRUE(Storage.exists(previousStatsPath.c_str()));
  Storage.failNextRemove(previousStatsPath);

  EXPECT_FALSE(BookReadingStats::remove(CACHE_PATH));

  EXPECT_TRUE(Storage.exists(statsPath.c_str()));
  EXPECT_EQ(BookReadingStats::load(CACHE_PATH).totalReadingSeconds, 456U);
}

TEST_F(BookReadingStatsAtomicTest, FutureVersionIsNeverOverwritten) {
  const std::string path = statsPath;
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", path, file));
  uint8_t data[74]{};
  data[0] = 6;
  ASSERT_EQ(file.write(data, sizeof(data)), sizeof(data));
  file.close();
  const auto stats = BookReadingStats::load(CACHE_PATH);
  EXPECT_FALSE(stats.writable);
  EXPECT_FALSE(stats.save(CACHE_PATH));
  ASSERT_TRUE(Storage.openFileForRead("TEST", path, file));
  EXPECT_EQ(file.fileSize(), 74U);
  uint8_t version = 0;
  ASSERT_EQ(file.read(&version, 1), 1);
  EXPECT_EQ(version, 6);
}

TEST_F(BookReadingStatsAtomicTest, TruncatedPrimaryIsPreserved) {
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", statsPath, file));
  file.write(uint8_t{5});
  file.close();
  auto stats = BookReadingStats::load(CACHE_PATH);
  EXPECT_FALSE(stats.writable);
  EXPECT_FALSE(stats.save(CACHE_PATH));
}

TEST(ReadingStatsCalendar, RejectsInvalidTimesAndHandlesLeapDay) {
  EXPECT_FALSE((ReadingStatsDateTime{{2026, 11, 3}, 24, 0, 0}).isValid());
  EXPECT_FALSE((ReadingStatsDateTime{{2026, 11, 3}, 12, 60, 0}).isValid());
  EXPECT_TRUE((ReadingStatsDate{2024, 2, 29}).isValid());
  EXPECT_FALSE((ReadingStatsDate{2026, 2, 29}).isValid());
  auto date = ReadingStatsDate{2024, 2, 28};
  addDaysToReadingStatsDate(date, 2);
  EXPECT_EQ(date.month, 3);
  EXPECT_EQ(date.day, 1);
}

TEST(ReadingStatsCalendar, SplitsMidnightAndDoesNotTreatFutureDaysAsCurrentStreak) {
  std::array<uint32_t, READING_TIME_BUCKET_COUNT> time{};
  std::array<uint32_t, READING_DAY_OF_WEEK_COUNT> days{};
  const ReadingStatsDateTime start{{2026, 11, 3}, 23, 59, 30};
  recordReadingSpanIntoBuckets(time, days, start, 90);
  EXPECT_EQ(days[1], 30U);
  EXPECT_EQ(days[2], 60U);
  EXPECT_EQ(time[static_cast<size_t>(ReadingTimeBucket::Night)], 90U);
  uint32_t anchor = 0;
  std::array<uint8_t, READING_HISTORY_BYTES> history{};
  recordReadingSpanIntoHistory(anchor, history, start, 90);
  EXPECT_EQ(computeReadingHistoryLongestStreak(anchor, history), 2);
  const ReadingStatsDate yesterday{2026, 11, 2};
  EXPECT_EQ(computeReadingHistoryCurrentStreak(anchor, history, &yesterday), 0);
}

TEST_F(BookReadingStatsAtomicTest, UnreadableCurrentNeverFallsBackToLegacy) {
  ASSERT_TRUE(statsWithSeconds(1000).save(CACHE_PATH));
  HalFile legacy;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", std::string(CACHE_PATH) + "/stats.bin", legacy));
  const uint8_t bytes[11] = {1, 0, 0, 50, 0, 0, 0, 0, 0, 0, 0};
  legacy.write(bytes, sizeof(bytes));
  legacy.close();
  Storage.failReadsFor(statsPath);
  const auto blocked = BookReadingStats::load(CACHE_PATH);
  EXPECT_FALSE(blocked.writable);
  EXPECT_FALSE(blocked.save(CACHE_PATH));
}

TEST_F(BookReadingStatsAtomicTest, RemainingTimePrefersCachedEstimateAndBoundsFallback) {
  BookReadingStats stats;
  stats.totalReadingSeconds = 120;
  EXPECT_EQ(stats.remainingReadingSeconds(5000), 120U);
  stats.estimatedTimeLeftSeconds = 42;
  EXPECT_EQ(stats.remainingReadingSeconds(-1), 0U);
  EXPECT_EQ(stats.remainingReadingSeconds(5000), 120U);
  EXPECT_EQ(stats.remainingReadingSeconds(7500), 40U);
  stats.isCompleted = true;
  EXPECT_EQ(stats.remainingReadingSeconds(5000), 0U);
  stats.isCompleted = false;
  stats.estimatedTimeLeftSeconds = 0;
  EXPECT_EQ(stats.remainingReadingSeconds(0), 0U);
  EXPECT_EQ(stats.remainingReadingSeconds(10000), 0U);
  stats.totalReadingSeconds = 119;
  EXPECT_EQ(stats.remainingReadingSeconds(5000), 0U);
  stats.totalReadingSeconds = UINT32_MAX;
  EXPECT_EQ(stats.remainingReadingSeconds(1), UINT32_MAX);
  stats.writable = false;
  EXPECT_EQ(stats.remainingReadingSeconds(5000), 0U);
}

TEST_F(BookReadingStatsAtomicTest, ProjectedFinishUsesDailyPaceAndHandlesDateLimits) {
  BookReadingStats stats;
  stats.totalReadingSeconds = 1800;
  stats.startDate = {2026, 11, 1};
  stats.estimatedTimeLeftSeconds = 3600;
  auto date = stats.projectedFinishDate({{2026, 11, 4}, 12, 0, 0}, 5000);
  EXPECT_EQ(date.year, 2026);
  EXPECT_EQ(date.month, 11);
  EXPECT_EQ(date.day, 7);
  stats.startDate.clear();
  date = stats.projectedFinishDate({{2026, 11, 4}, 23, 30, 0}, 5000);
  EXPECT_EQ(date.day, 5);
  stats.startDate = {2026, 11, 4};
  stats.totalReadingSeconds = UINT32_MAX;
  stats.estimatedTimeLeftSeconds = 1;
  date = stats.projectedFinishDate({{2026, 11, 4}, 23, 59, 59}, 5000);
  EXPECT_EQ(date.day, 5);
  stats.startDate = {2000, 1, 1};
  stats.totalReadingSeconds = 1;
  stats.estimatedTimeLeftSeconds = UINT32_MAX;
  EXPECT_FALSE(stats.projectedFinishDate({{2099, 12, 31}, 23, 59, 59}, 5000).isValid());
  EXPECT_FALSE(stats.projectedFinishDate({}, 5000).isValid());
}

TEST_F(BookReadingStatsAtomicTest, FailedRotationAliasPreservesDataAndBlocksFurtherWrites) {
  ASSERT_TRUE(statsWithSeconds(123).save(CACHE_PATH));
  Storage.failNextAliasRenameFrom(statsPath);
  EXPECT_FALSE(statsWithSeconds(456).save(CACHE_PATH));
  const auto loaded = BookReadingStats::load(CACHE_PATH);
  EXPECT_EQ(loaded.totalReadingSeconds, 123U);
  EXPECT_FALSE(loaded.writable);
  EXPECT_TRUE(Storage.exists(statsPath.c_str()));
  EXPECT_TRUE(Storage.exists((statsPath + ".bak").c_str()));
  EXPECT_TRUE(Storage.exists((statsPath + ".tmp").c_str()));
  EXPECT_FALSE(BookReadingStats::remove(CACHE_PATH));
}
