#include <Arduino.h>
#include <DeviceStatsBackup.h>
#include <DeviceStatsRetention.h>
#include <GlobalReadingStats.h>
#include <HalStorage.h>
#include <Memory.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <string>

namespace {
uint32_t nextSequence = 42;
bool sequenceAvailable = true;
std::string latestName, prunedName;
}  // namespace
namespace readingstats {
bool normalStatsAllowed() { return true; }
}  // namespace readingstats
namespace statsbackup {
// The on-card scanner has separate real-filesystem tests. These controlled
// outputs exercise the enabled writer's naming, deduplication, and failure flow.
bool nextDeviceBackupSequence(char, uint32_t& next, char* latest, size_t size) {
  if (!sequenceAvailable) return false;
  next = nextSequence;
  if (latest) std::snprintf(latest, size, "%s", latestName.c_str());
  return true;
}
RetentionResult pruneAutomaticDeviceBackups(const char* current) {
  prunedName = current;
  return {};
}
}  // namespace statsbackup
class DeviceStatsEnabledBackupTest : public testing::Test {
 protected:
  void SetUp() override {
    Storage.reset();
    failNextBackupAllocation = false;
    nextSequence = 42;
    sequenceAvailable = true;
    latestName.clear();
    prunedName.clear();
    GlobalReadingStats stats;
    stats.totalReadingSeconds = 123;
    ASSERT_TRUE(stats.save());
  }
};
TEST_F(DeviceStatsEnabledBackupTest, ManualUsesPersistedSequenceAndPrunesItsOwnGroup) {
  testMillis = 3;
  ASSERT_TRUE(statsbackup::createDeviceBackup(GlobalReadingStats::loadReadOnly()));
  EXPECT_TRUE(prunedName.starts_with("stats_cp_0000000042_M_"));
  GlobalReadingStats read;
  EXPECT_EQ(statsbackup::inspectDeviceBackup(prunedName, read), statsbackup::BackupIntegrity::Checked);
  EXPECT_EQ(read.totalReadingSeconds, 123U);
}
TEST_F(DeviceStatsEnabledBackupTest, ClocklessDeduplicatesLatestThenAdvancesAfterChangedTotals) {
  EXPECT_EQ(statsbackup::createAutomaticDeviceBackup({}), statsbackup::AutomaticBackupResult::Saved);
  EXPECT_TRUE(prunedName.starts_with("stats_cp_0000000042_U_"));
  latestName = prunedName;
  nextSequence = 43;
  EXPECT_EQ(statsbackup::createAutomaticDeviceBackup({}), statsbackup::AutomaticBackupResult::AlreadyPresent);
  EXPECT_EQ(prunedName, latestName);
  auto stats = GlobalReadingStats::loadReadOnly();
  stats.totalReadingSeconds = 456;
  ASSERT_TRUE(stats.save());
  EXPECT_EQ(statsbackup::createAutomaticDeviceBackup({}), statsbackup::AutomaticBackupResult::Saved);
  EXPECT_TRUE(prunedName.starts_with("stats_cp_0000000043_U_"));
  EXPECT_TRUE(Storage.exists((std::string("/.crossink-stats-backup/") + latestName).c_str()));
}
TEST_F(DeviceStatsEnabledBackupTest, FailedSequenceScanStopsBothWriters) {
  sequenceAvailable = false;
  EXPECT_FALSE(statsbackup::createDeviceBackup(GlobalReadingStats::loadReadOnly()));
  EXPECT_EQ(statsbackup::createAutomaticDeviceBackup({}), statsbackup::AutomaticBackupResult::Unavailable);
  EXPECT_TRUE(prunedName.empty());
}
TEST_F(DeviceStatsEnabledBackupTest, UnreadableLatestClocklessSnapshotStopsCreation) {
  ASSERT_EQ(statsbackup::createAutomaticDeviceBackup({}), statsbackup::AutomaticBackupResult::Saved);
  latestName = prunedName;
  nextSequence = 43;
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", (std::string("/.crossink-stats-backup/") + latestName).c_str(), file));
  ASSERT_EQ(file.write(uint8_t{99}), 1U);
  ASSERT_TRUE(file.close());
  prunedName.clear();
  EXPECT_EQ(statsbackup::createAutomaticDeviceBackup({}), statsbackup::AutomaticBackupResult::Unavailable);
  EXPECT_TRUE(prunedName.empty());
}
