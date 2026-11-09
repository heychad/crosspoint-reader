#include <GlobalReadingStats.h>
#include <HalStorage.h>
#include <esp_mac.h>
#include <gtest/gtest.h>

namespace {
constexpr char PATH[] = "/.crosspoint/global_stats.bin";
class GlobalReadingStatsTest : public testing::Test {
  void SetUp() override { Storage.reset(); }
};
void writeBytes(const char* path, const std::vector<uint8_t>& bytes) {
  HalFile f;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", path, f));
  ASSERT_EQ(f.write(bytes.data(), bytes.size()), bytes.size());
  ASSERT_TRUE(f.close());
}
}  // namespace

TEST_F(GlobalReadingStatsTest, RoundTripAndInterruptedRenamePreserveOldHistory) {
  GlobalReadingStats stats;
  stats.totalReadingSeconds = 987;
  stats.totalSessions = 3;
  ASSERT_TRUE(stats.save());
  EXPECT_EQ(GlobalReadingStats::load().totalReadingSeconds, 987U);
  stats.totalReadingSeconds = 1234;
  Storage.failNextRenameFrom(std::string(PATH) + ".tmp");
  EXPECT_FALSE(stats.save());
  EXPECT_EQ(GlobalReadingStats::load().totalReadingSeconds, 987U);
}

TEST_F(GlobalReadingStatsTest, RecoversCompleteOrphanTemp) {
  GlobalReadingStats stats;
  stats.totalReadingSeconds = 789;
  ASSERT_TRUE(stats.save());
  ASSERT_TRUE(Storage.rename(PATH, "/.crosspoint/global_stats.bin.tmp"));
  const auto recovered = GlobalReadingStats::load();
  EXPECT_EQ(recovered.totalReadingSeconds, 789U);
  EXPECT_TRUE(recovered.writable);
  EXPECT_TRUE(Storage.exists(PATH));
}

TEST_F(GlobalReadingStatsTest, ReadOnlyLoadDoesNotPromoteOrphanTemp) {
  GlobalReadingStats stats;
  stats.totalReadingSeconds = 789;
  ASSERT_TRUE(stats.save());
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().totalReadingSeconds, 789U);
  ASSERT_TRUE(Storage.rename(PATH, "/.crosspoint/global_stats.bin.tmp"));
  EXPECT_FALSE(GlobalReadingStats::loadReadOnly().writable);
  EXPECT_FALSE(Storage.exists(PATH));
  EXPECT_TRUE(Storage.exists("/.crosspoint/global_stats.bin.tmp"));
  EXPECT_EQ(GlobalReadingStats::load().totalReadingSeconds, 789U);
  EXPECT_TRUE(Storage.exists(PATH));
}

TEST_F(GlobalReadingStatsTest, ReadOnlyLoadRejectsFailedClose) {
  GlobalReadingStats stats;
  stats.totalReadingSeconds = 789;
  ASSERT_TRUE(stats.save());
  Storage.failNextCloseFor(PATH);
  EXPECT_FALSE(GlobalReadingStats::loadReadOnly().writable);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().totalReadingSeconds, 789U);
}

TEST_F(GlobalReadingStatsTest, RejectsFutureAndTruncatedFiles) {
  writeBytes(PATH, {4});
  auto stats = GlobalReadingStats::load();
  EXPECT_FALSE(stats.writable);
  EXPECT_FALSE(stats.save());
  Storage.reset();
  writeBytes(PATH, {3, 1, 2});
  stats = GlobalReadingStats::load();
  EXPECT_FALSE(stats.writable);
  EXPECT_FALSE(stats.save());
}

TEST_F(GlobalReadingStatsTest, ReadsOlderFormatWithoutInventingNewCounters) {
  std::vector<uint8_t> bytes(13, 0);
  bytes[0] = 1;
  bytes[1] = 2;
  bytes[5] = 120;
  writeBytes(PATH, bytes);
  const auto stats = GlobalReadingStats::load();
  EXPECT_TRUE(stats.writable);
  EXPECT_EQ(stats.totalSessions, 2U);
  EXPECT_EQ(stats.totalReadingSeconds, 120U);
  EXPECT_EQ(stats.completedBooks, 0U);
}

TEST_F(GlobalReadingStatsTest, UnreadablePrimaryNeverFallsBackToOlderBackup) {
  GlobalReadingStats stats;
  stats.totalReadingSeconds = 100;
  ASSERT_TRUE(stats.save());
  stats.totalReadingSeconds = 200;
  ASSERT_TRUE(stats.save());
  Storage.failReadsFor(PATH);
  const auto blocked = GlobalReadingStats::load();
  EXPECT_FALSE(blocked.writable);
  EXPECT_FALSE(blocked.save());
}

TEST_F(GlobalReadingStatsTest, ValidBackupStaysReadOnlyWhenPrimaryIsMissingOrCorrupt) {
  constexpr char backup[] = "/.crosspoint/global_stats.bin.bak";
  for (const bool corruptPrimary : {false, true}) {
    Storage.reset();
    GlobalReadingStats original;
    original.totalReadingSeconds = 987;
    ASSERT_TRUE(original.save());
    ASSERT_TRUE(Storage.rename(PATH, backup));
    if (corruptPrimary) writeBytes(PATH, {3, 1, 2});
    auto recovered = GlobalReadingStats::load();
    EXPECT_EQ(recovered.totalReadingSeconds, 987U);
    EXPECT_FALSE(recovered.writable);
    recovered.totalReadingSeconds = 1234;
    Storage.failNextRenameFrom(PATH);
    EXPECT_FALSE(recovered.save());
    EXPECT_TRUE(Storage.exists(backup));
    auto again = GlobalReadingStats::load();
    EXPECT_EQ(again.totalReadingSeconds, 987U);
    EXPECT_FALSE(again.writable);
  }
}

TEST_F(GlobalReadingStatsTest, AggregationCountsPeersAndPreservesLocalRecovery) {
  ASSERT_TRUE(Storage.mkdir("/.crosspoint/synced_stats"));
  GlobalReadingStats peer;
  peer.totalSessions = 4;
  peer.totalReadingSeconds = 600;
  ASSERT_TRUE(peer.save());
  ASSERT_TRUE(Storage.rename(PATH, "/.crosspoint/synced_stats/device_aabbccddeeff.bin"));
  GlobalReadingStats local;
  local.totalSessions = 2;
  const auto total = GlobalReadingStats::loadAggregated(local);
  EXPECT_TRUE(total.complete);
  EXPECT_EQ(total.sources, 2U);
  EXPECT_EQ(total.stats.totalSessions, 6U);
  EXPECT_EQ(total.stats.totalReadingSeconds, 600U);
  EXPECT_TRUE(Storage.exists("/.crosspoint/synced_stats/device_aabbccddeeff.bin"));
}

TEST_F(GlobalReadingStatsTest, AggregationReportsUnreadablePeer) {
  ASSERT_TRUE(Storage.mkdir("/.crosspoint/synced_stats"));
  writeBytes("/.crosspoint/synced_stats/device_aabbccddeeff.bin", {255});
  const auto total = GlobalReadingStats::loadAggregated(GlobalReadingStats{});
  EXPECT_FALSE(total.complete);
  EXPECT_EQ(total.sources, 1U);
  EXPECT_EQ(total.skipped, 1U);
}

TEST_F(GlobalReadingStatsTest, FailedRotationAliasPreservesDataAndBlocksFurtherWrites) {
  GlobalReadingStats original;
  original.totalReadingSeconds = 123;
  ASSERT_TRUE(original.save());
  auto changed = original;
  changed.totalReadingSeconds = 456;
  Storage.failNextAliasRenameFrom(PATH);
  EXPECT_FALSE(changed.save());
  const auto loaded = GlobalReadingStats::load();
  EXPECT_EQ(loaded.totalReadingSeconds, 123U);
  EXPECT_FALSE(loaded.writable);
  EXPECT_TRUE(Storage.exists(PATH));
  EXPECT_TRUE(Storage.exists("/.crosspoint/global_stats.bin.bak"));
  EXPECT_TRUE(Storage.exists("/.crosspoint/global_stats.bin.tmp"));
}
