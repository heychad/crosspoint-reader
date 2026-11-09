#include <Arduino.h>
#include <DeviceStatsBackup.h>
#include <GlobalReadingStats.h>
#include <HalStorage.h>
#include <Memory.h>
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
