#include <Arduino.h>
#include <DeviceStatsBackup.h>
#include <GlobalReadingStats.h>
#include <HalStorage.h>
#include <Memory.h>
#include <StatsSessionLease.h>
#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

namespace {
constexpr char DIRECTORY[] = "/.crossink-stats-backup";

std::vector<std::string> names() {
  std::vector<std::string> result;
  auto directory = Storage.open(DIRECTORY);
  if (!directory) return result;
  for (auto entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    char name[128]{};
    entry.getName(name, sizeof(name));
    result.emplace_back(name);
  }
  return result;
}

GlobalReadingStats fixture() {
  GlobalReadingStats stats;
  stats.totalReadingSeconds = 1234;
  stats.totalSessions = 3;
  stats.readingHistoryBits[2] = 7;
  return stats;
}

class DeviceStatsBackupTest : public testing::Test {
 protected:
  void SetUp() override {
    Storage.reset();
    testMillis = 7;
    failNextBackupAllocation = false;
    ASSERT_TRUE(fixture().save());
  }
};
}  // namespace

TEST_F(DeviceStatsBackupTest, CreatesVerifiedCompatibleSnapshot) {
  const auto stats = fixture();
  ASSERT_TRUE(statsbackup::createDeviceBackup(stats));
  const auto files = names();
  ASSERT_EQ(files.size(), 1U);
  EXPECT_EQ(files[0].size(), 58U);
  EXPECT_TRUE(files[0].starts_with("stats_cp_0000000008_M_00000000_000000_"));
  EXPECT_TRUE(files[0].ends_with(".bin"));
  HalFile file;
  ASSERT_TRUE(Storage.openFileForRead("TEST", std::string(DIRECTORY) + "/" + files[0], file));
  std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> actual{}, expected{};
  ASSERT_TRUE(stats.encode(expected));
  ASSERT_EQ(file.read(actual.data(), actual.size()), static_cast<int>(actual.size()));
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(actual[0], GlobalReadingStats::CURRENT_FILE_VERSION);
  EXPECT_EQ(actual[1], 3U);     // Sessions, little-endian.
  EXPECT_EQ(actual[5], 0xd2U);  // 1234 seconds, little-endian.
  EXPECT_EQ(actual[6], 0x04U);
  EXPECT_EQ(actual[67], 7U);  // Calendar-history byte.
}

TEST_F(DeviceStatsBackupTest, ReadsVerifiedBackupWithoutChangingCurrentStatistics) {
  ASSERT_TRUE(statsbackup::createDeviceBackup(fixture()));
  const auto name = names().at(0);
  auto current = fixture();
  current.totalReadingSeconds = 2000;
  ASSERT_TRUE(current.save());
  GlobalReadingStats restored;
  ASSERT_TRUE(statsbackup::readDeviceBackup(name, restored));
  EXPECT_EQ(restored.totalReadingSeconds, 1234U);
  EXPECT_EQ(restored.readingHistoryBits[2], 7U);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().totalReadingSeconds, 2000U);
}

TEST_F(DeviceStatsBackupTest, ReadsCrossInkLegacyNamesAndMarksThemUnverified) {
  ASSERT_TRUE(statsbackup::createDeviceBackup(fixture()));
  const auto checked = names().at(0);
  GlobalReadingStats output;
  EXPECT_EQ(statsbackup::inspectDeviceBackup(checked, output), statsbackup::BackupIntegrity::Checked);
  for (const auto& name : {"stats_2026-11-10.bin", "stats_2026-11-10_0912.bin", "stats_backup_001.bin"}) {
    const std::string path = std::string(DIRECTORY) + "/" + name;
    HalFile file;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", path, file));
    const std::array<uint8_t, 13> old{1, 2, 0, 0, 0, 120, 0, 0, 0, 5, 0, 0, 0};
    ASSERT_EQ(file.write(old.data(), old.size()), old.size());
    ASSERT_TRUE(file.close());
    output.totalReadingSeconds = 7654;
    EXPECT_EQ(statsbackup::inspectDeviceBackup(name, output), statsbackup::BackupIntegrity::LegacyUnverified);
    EXPECT_EQ(output.totalReadingSeconds, 120U);
    EXPECT_EQ(output.totalSessions, 2U);
  }
}

TEST_F(DeviceStatsBackupTest, RejectsBadLegacyNameOrPayloadWithoutChangingOutput) {
  ASSERT_TRUE(statsbackup::createDeviceBackup(fixture()));
  const std::string name = "stats_2026-11-10.bin";
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", std::string(DIRECTORY) + "/" + name, file));
  const uint8_t bad[] = {3, 1, 2};
  ASSERT_EQ(file.write(bad, sizeof(bad)), sizeof(bad));
  ASSERT_TRUE(file.close());
  GlobalReadingStats output;
  output.totalReadingSeconds = 7654;
  EXPECT_EQ(statsbackup::inspectDeviceBackup(name, output), statsbackup::BackupIntegrity::Unavailable);
  EXPECT_EQ(statsbackup::inspectDeviceBackup("../" + name, output), statsbackup::BackupIntegrity::Unavailable);
  EXPECT_EQ(statsbackup::inspectDeviceBackup("stats_backup_000.bin", output),
            statsbackup::BackupIntegrity::Unavailable);
  EXPECT_EQ(output.totalReadingSeconds, 7654U);
}

TEST_F(DeviceStatsBackupTest, RestoresLegacySnapshotAfterBackingUpCurrentTotals) {
  ASSERT_TRUE(statsbackup::createDeviceBackup(fixture()));
  const std::string legacy = "stats_2026-11-10.bin";
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", std::string(DIRECTORY) + "/" + legacy, file));
  const std::array<uint8_t, 13> old{1, 2, 0, 0, 0, 120, 0, 0, 0, 5, 0, 0, 0};
  ASSERT_EQ(file.write(old.data(), old.size()), old.size());
  ASSERT_TRUE(file.close());
  ++testMillis;
  EXPECT_EQ(statsbackup::restoreDeviceBackup(legacy), statsbackup::RestoreResult::Restored);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().totalReadingSeconds, 120U);
  EXPECT_EQ(names().size(), 3U);
}

TEST_F(DeviceStatsBackupTest, RejectsDamagedUnreadableOrInvalidBackupWithoutChangingOutput) {
  ASSERT_TRUE(statsbackup::createDeviceBackup(fixture()));
  const auto name = names().at(0);
  const std::string path = std::string(DIRECTORY) + "/" + name;
  GlobalReadingStats output;
  output.totalReadingSeconds = 7654;
  EXPECT_FALSE(statsbackup::readDeviceBackup("../" + name, output));
  Storage.failNextCloseFor(path);
  EXPECT_FALSE(statsbackup::readDeviceBackup(name, output));
  EXPECT_EQ(output.totalReadingSeconds, 7654U);
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", path, file));
  std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> bytes{};
  ASSERT_TRUE(fixture().encode(bytes));
  bytes[5] ^= 1;
  ASSERT_EQ(file.write(bytes.data(), bytes.size()), bytes.size());
  ASSERT_TRUE(file.close());
  EXPECT_FALSE(statsbackup::readDeviceBackup(name, output));
  EXPECT_EQ(output.totalReadingSeconds, 7654U);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().totalReadingSeconds, 1234U);
}

TEST_F(DeviceStatsBackupTest, RestorePreservesCurrentSnapshotAndReplacesDeviceTotals) {
  ASSERT_TRUE(statsbackup::createDeviceBackup(fixture()));
  const auto name = names().at(0);
  auto newer = fixture();
  newer.totalReadingSeconds = 2000;
  ASSERT_TRUE(newer.save());
  ++testMillis;
  EXPECT_EQ(statsbackup::restoreDeviceBackup(name), statsbackup::RestoreResult::Restored);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().totalReadingSeconds, 1234U);
  const auto backups = names();
  ASSERT_EQ(backups.size(), 2U);
  bool foundPrevious = false;
  for (const auto& backup : backups) {
    GlobalReadingStats restored;
    ASSERT_TRUE(statsbackup::readDeviceBackup(backup, restored));
    if (restored.totalReadingSeconds == 2000U) foundPrevious = true;
  }
  EXPECT_TRUE(foundPrevious);
}

TEST_F(DeviceStatsBackupTest, RestoreRefusesActiveReaderAndUnverifiedFile) {
  ASSERT_TRUE(statsbackup::createDeviceBackup(fixture()));
  const auto name = names().at(0);
  auto newer = fixture();
  newer.totalReadingSeconds = 2000;
  ASSERT_TRUE(newer.save());
  ++testMillis;
  {
    StatsSessionLease lease;
    ASSERT_TRUE(lease.acquire());
    EXPECT_EQ(statsbackup::restoreDeviceBackup(name), statsbackup::RestoreResult::Busy);
  }
  EXPECT_EQ(statsbackup::restoreDeviceBackup("../" + name), statsbackup::RestoreResult::Unavailable);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().totalReadingSeconds, 2000U);
  EXPECT_EQ(names().size(), 1U);
}

TEST_F(DeviceStatsBackupTest, RestoreRefusesUnverifiedStorageIdentity) {
  ASSERT_TRUE(statsbackup::createDeviceBackup(fixture()));
  const auto name = names().at(0);
  Storage.failSettingsIdentity();
  ++testMillis;
  EXPECT_EQ(statsbackup::restoreDeviceBackup(name), statsbackup::RestoreResult::Unavailable);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().totalReadingSeconds, 1234U);
  EXPECT_EQ(names().size(), 1U);
}

TEST_F(DeviceStatsBackupTest, RestoreRefusesTotalsChangedSinceConfirmation) {
  ASSERT_TRUE(statsbackup::createDeviceBackup(fixture()));
  const auto name = names().at(0);
  std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> preview{};
  ASSERT_TRUE(fixture().encode(preview));
  auto changed = fixture();
  changed.totalReadingSeconds = 2000;
  ASSERT_TRUE(changed.save());
  ++testMillis;
  EXPECT_EQ(statsbackup::restoreDeviceBackup(name, preview.data(), preview.size()),
            statsbackup::RestoreResult::Unavailable);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().totalReadingSeconds, 2000U);
  EXPECT_EQ(names().size(), 1U);
}

TEST_F(DeviceStatsBackupTest, RestoreRefusesBackupDifferentFromConfirmedPreview) {
  ASSERT_TRUE(statsbackup::createDeviceBackup(fixture()));
  const auto name = names().at(0);
  auto changed = fixture();
  changed.totalReadingSeconds = 2000;
  ASSERT_TRUE(changed.save());
  std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> current{}, wrongBackup{};
  ASSERT_TRUE(changed.encode(current));
  ASSERT_TRUE(changed.encode(wrongBackup));
  ++testMillis;
  EXPECT_EQ(
      statsbackup::restoreDeviceBackup(name, current.data(), current.size(), wrongBackup.data(), wrongBackup.size()),
      statsbackup::RestoreResult::Unavailable);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().totalReadingSeconds, 2000U);
  EXPECT_EQ(names().size(), 1U);
}

TEST_F(DeviceStatsBackupTest, FailedRestoreRetainsSourceAndPreRestoreSnapshot) {
  ASSERT_TRUE(statsbackup::createDeviceBackup(fixture()));
  const auto name = names().at(0);
  auto newer = fixture();
  newer.totalReadingSeconds = 2000;
  ASSERT_TRUE(newer.save());
  ++testMillis;
  Storage.failNextRenameFrom("/.crosspoint/global_stats.bin.tmp");
  EXPECT_EQ(statsbackup::restoreDeviceBackup(name), statsbackup::RestoreResult::WriteUncertain);
  EXPECT_TRUE(Storage.exists((std::string(DIRECTORY) + "/" + name).c_str()));
  EXPECT_EQ(names().size(), 2U);
  EXPECT_TRUE(Storage.exists("/.crosspoint/global_stats.bin.tmp"));
}

TEST_F(DeviceStatsBackupTest, ResetBacksUpAndClearsOnlyLocalDeviceTotals) {
  std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> expected{};
  ASSERT_TRUE(fixture().encode(expected));
  EXPECT_EQ(statsbackup::resetDeviceStats(expected.data(), expected.size()), statsbackup::RestoreResult::Restored);
  const auto current = GlobalReadingStats::loadReadOnly();
  EXPECT_TRUE(current.writable);
  EXPECT_EQ(current.totalReadingSeconds, 0U);
  EXPECT_EQ(current.totalSessions, 0U);
  ASSERT_EQ(names().size(), 1U);
  GlobalReadingStats backup;
  ASSERT_TRUE(statsbackup::readDeviceBackup(names().at(0), backup));
  EXPECT_EQ(backup.totalReadingSeconds, 1234U);
  EXPECT_EQ(backup.readingHistoryBits[2], 7U);
}

TEST_F(DeviceStatsBackupTest, ResetRefusesLiveSessionAndChangedTotals) {
  std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> expected{};
  ASSERT_TRUE(fixture().encode(expected));
  {
    StatsSessionLease lease;
    ASSERT_TRUE(lease.acquire());
    EXPECT_EQ(statsbackup::resetDeviceStats(expected.data(), expected.size()), statsbackup::RestoreResult::Busy);
  }
  expected[5] ^= 1;
  EXPECT_EQ(statsbackup::resetDeviceStats(expected.data(), expected.size()), statsbackup::RestoreResult::Unavailable);
  EXPECT_TRUE(names().empty());
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().totalReadingSeconds, 1234U);
}

TEST_F(DeviceStatsBackupTest, ResetWriteFailureKeepsBackupAndUncertainTemp) {
  Storage.failNextRenameFrom("/.crosspoint/global_stats.bin.tmp");
  EXPECT_EQ(statsbackup::resetDeviceStats(), statsbackup::RestoreResult::WriteUncertain);
  ASSERT_EQ(names().size(), 1U);
  GlobalReadingStats backup;
  ASSERT_TRUE(statsbackup::readDeviceBackup(names().at(0), backup));
  EXPECT_EQ(backup.totalReadingSeconds, 1234U);
  EXPECT_TRUE(Storage.exists("/.crosspoint/global_stats.bin.tmp"));
}

TEST_F(DeviceStatsBackupTest, DailyBackupsPreserveChangesAndDeduplicateIdenticalSnapshots) {
  const ReadingStatsDate date{2026, 11, 10};
  EXPECT_EQ(statsbackup::createAutomaticDeviceBackup(date), statsbackup::AutomaticBackupResult::Saved);
  EXPECT_EQ(statsbackup::createAutomaticDeviceBackup(date), statsbackup::AutomaticBackupResult::AlreadyPresent);
  ASSERT_EQ(names().size(), 1U);
  GlobalReadingStats backup;
  EXPECT_EQ(statsbackup::inspectDeviceBackup(names().at(0), backup), statsbackup::BackupIntegrity::Checked);
  EXPECT_EQ(backup.totalReadingSeconds, 1234U);
  auto changed = fixture();
  changed.totalReadingSeconds = 2000;
  ASSERT_TRUE(changed.save());
  EXPECT_EQ(statsbackup::createAutomaticDeviceBackup(date), statsbackup::AutomaticBackupResult::Saved);
  EXPECT_EQ(names().size(), 2U);
  EXPECT_EQ(statsbackup::createAutomaticDeviceBackup({2026, 11, 11}), statsbackup::AutomaticBackupResult::Saved);
  EXPECT_EQ(names().size(), 3U);
}

TEST_F(DeviceStatsBackupTest, DailyBackupRefusesInvalidDateAndActiveReader) {
  EXPECT_EQ(statsbackup::createAutomaticDeviceBackup({2026, 2, 30}), statsbackup::AutomaticBackupResult::Unavailable);
  {
    StatsSessionLease lease;
    ASSERT_TRUE(lease.acquire());
    EXPECT_EQ(statsbackup::createAutomaticDeviceBackup({2026, 11, 10}),
              statsbackup::AutomaticBackupResult::Unavailable);
  }
  EXPECT_TRUE(names().empty());
}

TEST_F(DeviceStatsBackupTest, DailyBackupDoesNotReplaceDamagedExistingSnapshot) {
  const ReadingStatsDate date{2026, 11, 10};
  ASSERT_EQ(statsbackup::createAutomaticDeviceBackup(date), statsbackup::AutomaticBackupResult::Saved);
  const auto name = names().at(0);
  const auto path = std::string(DIRECTORY) + "/" + name;
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", path, file));
  ASSERT_EQ(file.write(uint8_t{42}), 1U);
  ASSERT_TRUE(file.close());
  EXPECT_EQ(statsbackup::createAutomaticDeviceBackup(date), statsbackup::AutomaticBackupResult::Unavailable);
  ASSERT_TRUE(Storage.openFileForRead("TEST", path, file));
  EXPECT_EQ(file.fileSize(), 1U);
  EXPECT_TRUE(file.close());
}

TEST_F(DeviceStatsBackupTest, DailyBackupRequiresDistinctVerifiedStorageChains) {
  const ReadingStatsDate date{2026, 11, 10};
  ASSERT_EQ(statsbackup::createAutomaticDeviceBackup(date), statsbackup::AutomaticBackupResult::Saved);
  Storage.failSettingsIdentity();
  EXPECT_EQ(statsbackup::createAutomaticDeviceBackup(date), statsbackup::AutomaticBackupResult::Unavailable);
  EXPECT_EQ(names().size(), 1U);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().totalReadingSeconds, 1234U);
}

TEST_F(DeviceStatsBackupTest, RepeatedBackupsNeverOverwrite) {
  ASSERT_TRUE(statsbackup::createDeviceBackup(fixture()));
  EXPECT_FALSE(statsbackup::createDeviceBackup(fixture()));
  ++testMillis;
  ASSERT_TRUE(statsbackup::createDeviceBackup(fixture()));
  const auto files = names();
  ASSERT_EQ(files.size(), 2U);
  EXPECT_NE(files[0], files[1]);
}

TEST_F(DeviceStatsBackupTest, UnavailableStateLookupOrMemoryDoesNotCreateBackup) {
  auto stats = fixture();
  stats.writable = false;
  EXPECT_FALSE(statsbackup::createDeviceBackup(stats));
  failNextBackupAllocation = true;
  EXPECT_FALSE(statsbackup::createDeviceBackup(fixture()));
  Storage.failReadsFor(DIRECTORY);
  EXPECT_FALSE(statsbackup::createDeviceBackup(fixture()));
  EXPECT_TRUE(names().empty());
}

TEST_F(DeviceStatsBackupTest, MissingOrUnreadablePrimaryDoesNotCreateEmptyBackup) {
  Storage.reset();
  EXPECT_FALSE(statsbackup::createDeviceBackup(fixture()));
  ASSERT_TRUE(fixture().save());
  Storage.failReadsFor("/.crosspoint/global_stats.bin");
  EXPECT_FALSE(statsbackup::createDeviceBackup(fixture()));
  EXPECT_TRUE(names().empty());
}

TEST_F(DeviceStatsBackupTest, SourceCloseFailureDoesNotCreateBackup) {
  Storage.failNextCloseFor("/.crosspoint/global_stats.bin");
  EXPECT_FALSE(statsbackup::createDeviceBackup(fixture()));
  EXPECT_TRUE(Storage.exists("/.crosspoint/global_stats.bin"));
  EXPECT_TRUE(names().empty());
}

TEST_F(DeviceStatsBackupTest, RejectsSnapshotThatNoLongerMatchesThePrimary) {
  auto changed = fixture();
  ++changed.totalReadingSeconds;
  ASSERT_TRUE(changed.save());
  EXPECT_FALSE(statsbackup::createDeviceBackup(fixture()));
  EXPECT_TRUE(names().empty());
  EXPECT_TRUE(statsbackup::createDeviceBackup(changed));
}

TEST_F(DeviceStatsBackupTest, FailedWritePreservesIncompleteTargetAndSource) {
  ASSERT_TRUE(statsbackup::createDeviceBackup(fixture()));
  const auto original = names().at(0);
  Storage.reset();
  ASSERT_TRUE(fixture().save());
  const std::string target = std::string(DIRECTORY) + "/" + original;
  Storage.failNextWriteFor(target, 10);
  EXPECT_FALSE(statsbackup::createDeviceBackup(fixture()));
  HalFile incomplete;
  ASSERT_TRUE(Storage.openFileForRead("TEST", target, incomplete));
  EXPECT_EQ(incomplete.fileSize(), 10U);
  EXPECT_TRUE(incomplete.close());
  EXPECT_TRUE(Storage.exists("/.crosspoint/global_stats.bin"));
  EXPECT_FALSE(statsbackup::createDeviceBackup(fixture()));
  ++testMillis;
  EXPECT_TRUE(statsbackup::createDeviceBackup(fixture()));
  ASSERT_TRUE(Storage.openFileForRead("TEST", target, incomplete));
  EXPECT_EQ(incomplete.fileSize(), 10U);
  EXPECT_TRUE(incomplete.close());
  EXPECT_EQ(names().size(), 2U);
}

TEST_F(DeviceStatsBackupTest, ExistingLegacyPartialIsPreservedAndBlocksSameName) {
  ASSERT_TRUE(statsbackup::createDeviceBackup(fixture()));
  const auto original = names().at(0);
  Storage.reset();
  ASSERT_TRUE(fixture().save());
  const std::string target = std::string(DIRECTORY) + "/" + original;
  const std::string partial = target + ".part";
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", partial, file));
  ASSERT_EQ(file.write(uint8_t{42}), 1U);
  ASSERT_TRUE(file.close());
  EXPECT_FALSE(statsbackup::createDeviceBackup(fixture()));
  EXPECT_TRUE(Storage.exists(partial.c_str()));
  EXPECT_FALSE(Storage.exists(target.c_str()));
}

TEST_F(DeviceStatsBackupTest, FailedTargetClosePreservesFileAndPreventsOverwrite) {
  ASSERT_TRUE(statsbackup::createDeviceBackup(fixture()));
  const auto name = names().at(0);
  Storage.reset();
  ASSERT_TRUE(fixture().save());
  const std::string target = std::string(DIRECTORY) + "/" + name;
  Storage.failNextNewCloseFor(target);
  EXPECT_FALSE(statsbackup::createDeviceBackup(fixture()));
  EXPECT_TRUE(Storage.exists(target.c_str()));
  EXPECT_FALSE(statsbackup::createDeviceBackup(fixture()));
  ++testMillis;
  EXPECT_TRUE(statsbackup::createDeviceBackup(fixture()));
  EXPECT_EQ(names().size(), 2U);
}

TEST_F(DeviceStatsBackupTest, UndatedSnapshotsAreSeparateVerifiedAndDeduplicated) {
  EXPECT_EQ(statsbackup::createAutomaticDeviceBackup({}), statsbackup::AutomaticBackupResult::Saved);
  EXPECT_EQ(statsbackup::createAutomaticDeviceBackup({}), statsbackup::AutomaticBackupResult::AlreadyPresent);
  auto files = names();
  ASSERT_EQ(files.size(), 1U);
  EXPECT_TRUE(files[0].starts_with("stats_cp_00000000_U_"));
  GlobalReadingStats backup;
  EXPECT_EQ(statsbackup::inspectDeviceBackup(files[0], backup), statsbackup::BackupIntegrity::Checked);
  EXPECT_EQ(backup.totalReadingSeconds, 1234U);
  EXPECT_EQ(statsbackup::createAutomaticDeviceBackup({2026, 11, 11}), statsbackup::AutomaticBackupResult::Saved);
  EXPECT_TRUE(statsbackup::createDeviceBackup(fixture()));
  EXPECT_EQ(names().size(), 3U);
  auto changed = fixture();
  changed.totalReadingSeconds = 2222;
  ASSERT_TRUE(changed.save());
  EXPECT_EQ(statsbackup::createAutomaticDeviceBackup({}), statsbackup::AutomaticBackupResult::Saved);
  EXPECT_EQ(names().size(), 4U);
  EXPECT_EQ(statsbackup::inspectDeviceBackup(files[0], backup), statsbackup::BackupIntegrity::Checked);
  EXPECT_EQ(backup.totalReadingSeconds, 1234U);
}
