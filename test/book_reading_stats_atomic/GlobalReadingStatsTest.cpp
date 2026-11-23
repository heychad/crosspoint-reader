#include <CredentialIntegrity.h>
#include <GlobalReadingStats.h>
#include <HalStorage.h>
#include <esp_mac.h>
#include <gtest/gtest.h>

namespace {
constexpr char PATH[] = "/.crosspoint/global_stats.bin";
constexpr char CROSSINK[] = "/.crosspoint/global_stats_crossink-001122334455.bin";
constexpr char CROSSINK_BACKUP[] = "/.crosspoint/global_stats_crossink-001122334455.bin.bak";
class GlobalReadingStatsTest : public testing::Test {
  void SetUp() override { Storage.reset(); }
};
void writeBytes(const char* path, const std::vector<uint8_t>& bytes) {
  HalFile f;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", path, f));
  ASSERT_EQ(f.write(bytes.data(), bytes.size()), bytes.size());
  ASSERT_TRUE(f.close());
}
std::vector<uint8_t> crossInkSnapshot(const GlobalReadingStats& stats) {
  std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> native{};
  EXPECT_TRUE(stats.encode(native));
  std::vector<uint8_t> snapshot(native.begin(), native.end());
  snapshot.resize(GlobalReadingStats::CROSSINK_V4_FILE_SIZE, 0xA5);
  snapshot[0] = 4;
  return snapshot;
}

std::vector<uint8_t> crossInkRecord(std::vector<uint8_t> payload, uint32_t sequence) {
  const auto append = [&payload](uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) payload.push_back(static_cast<uint8_t>(value >> shift));
  };
  append(sequence);
  append(credential_integrity::crc32({reinterpret_cast<const char*>(payload.data()), payload.size()}));
  return payload;
}
}  // namespace

TEST_F(GlobalReadingStatsTest, DecodeChecksVersionAndSizeWithoutChangingOutputOnFailure) {
  GlobalReadingStats source;
  source.totalSessions = 7;
  source.totalReadingSeconds = 1234;
  source.completedBooks = 3;
  source.readingHistoryBits[4] = 42;
  std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> bytes{};
  ASSERT_TRUE(source.encode(bytes));
  GlobalReadingStats decoded;
  ASSERT_TRUE(GlobalReadingStats::decode(bytes.data(), bytes.size(), decoded));
  EXPECT_EQ(decoded.totalSessions, 7U);
  EXPECT_EQ(decoded.totalReadingSeconds, 1234U);
  EXPECT_EQ(decoded.completedBooks, 3U);
  EXPECT_EQ(decoded.readingHistoryBits[4], 42);
  EXPECT_FALSE(GlobalReadingStats::decode(nullptr, bytes.size(), decoded));
  EXPECT_FALSE(GlobalReadingStats::decode(bytes.data(), bytes.size() - 1, decoded));
  bytes[0] = GlobalReadingStats::CURRENT_FILE_VERSION + 1;
  EXPECT_FALSE(GlobalReadingStats::decode(bytes.data(), bytes.size(), decoded));
  EXPECT_EQ(decoded.totalReadingSeconds, 1234U);
  EXPECT_EQ(decoded.readingHistoryBits[4], 42);
}

TEST_F(GlobalReadingStatsTest, DecodeAcceptsLegacyPayloadWithCleanNewFields) {
  const std::array<uint8_t, 13> old{1, 2, 0, 0, 0, 120, 0, 0, 0, 5, 0, 0, 0};
  GlobalReadingStats decoded;
  decoded.completedBooks = 99;
  decoded.readingHistoryBits[4] = 42;
  ASSERT_TRUE(GlobalReadingStats::decode(old.data(), old.size(), decoded));
  EXPECT_EQ(decoded.totalSessions, 2U);
  EXPECT_EQ(decoded.totalReadingSeconds, 120U);
  EXPECT_EQ(decoded.completedBooks, 0U);
  EXPECT_EQ(decoded.readingHistoryBits[4], 0);
}

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

TEST_F(GlobalReadingStatsTest, ImportsNewestCrossInkSlotAndRetainsNativeReset) {
  GlobalReadingStats newer;
  newer.totalSessions = 7;
  newer.totalReadingSeconds = 830;
  newer.totalPagesTurned = 130;
  newer.completedBooks = 2;
  newer.timeOfDaySeconds[2] = 830;
  newer.dayOfWeekSeconds[5] = 830;
  newer.readingHistoryAnchorDay = 500;
  newer.readingHistoryBits[0] = 3;
  newer.longestReadingStreak = 2;
  GlobalReadingStats older = newer;
  older.totalReadingSeconds -= 300;
  const auto primary = crossInkRecord(crossInkSnapshot(older), UINT32_MAX);
  const auto backup = crossInkRecord(crossInkSnapshot(newer), 0);
  writeBytes(CROSSINK, primary);
  writeBytes(CROSSINK_BACKUP, backup);

  EXPECT_FALSE(GlobalReadingStats::loadReadOnly().writable);
  EXPECT_FALSE(Storage.exists(PATH));
  const auto imported = GlobalReadingStats::load();
  std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> expected{}, actual{};
  ASSERT_TRUE(newer.encode(expected));
  ASSERT_TRUE(imported.encode(actual));
  EXPECT_EQ(actual, expected);
  ASSERT_TRUE(GlobalReadingStats::loadReadOnly().encode(actual));
  EXPECT_EQ(actual, expected);

  ASSERT_TRUE(GlobalReadingStats{}.save());
  const auto reset = GlobalReadingStats::load();
  EXPECT_TRUE(reset.writable);
  EXPECT_EQ(reset.totalReadingSeconds, 0U);
  for (const auto* path : {CROSSINK, CROSSINK_BACKUP}) {
    const auto& source = path == CROSSINK ? primary : backup;
    HalFile file;
    ASSERT_TRUE(Storage.openFileForRead("TEST", path, file));
    EXPECT_EQ(file.fileSize(), source.size());
    std::vector<uint8_t> stored(source.size());
    ASSERT_EQ(file.read(stored.data(), stored.size()), static_cast<int>(stored.size()));
    ASSERT_TRUE(file.close());
    EXPECT_EQ(stored, source);
  }
}

TEST_F(GlobalReadingStatsTest, RecoversCrossInkFramedV3AfterV4SlotCrcFailure) {
  GlobalReadingStats older;
  older.totalReadingSeconds = 620;
  std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> v3{};
  ASSERT_TRUE(older.encode(v3));
  writeBytes(CROSSINK, crossInkRecord({v3.begin(), v3.end()}, 2));
  GlobalReadingStats newer;
  newer.totalReadingSeconds = 870;
  auto damaged = crossInkRecord(crossInkSnapshot(newer), 3);
  damaged.back() ^= 1;
  writeBytes(CROSSINK_BACKUP, damaged);
  EXPECT_EQ(GlobalReadingStats::load().totalReadingSeconds, 620U);
  EXPECT_EQ(GlobalReadingStats::loadReadOnly().totalReadingSeconds, 620U);
}

TEST_F(GlobalReadingStatsTest, ImportsRawCrossInkOnRetryAfterUncertainReadOrWrite) {
  GlobalReadingStats source;
  source.totalReadingSeconds = 720;
  writeBytes(CROSSINK, crossInkSnapshot(source));
  writeBytes(CROSSINK_BACKUP, crossInkSnapshot(GlobalReadingStats{}));
  Storage.failNextReadFor(CROSSINK_BACKUP);
  const auto readUncertain = GlobalReadingStats::load();
  EXPECT_FALSE(readUncertain.writable);
  EXPECT_EQ(readUncertain.totalReadingSeconds, 720U);
  EXPECT_FALSE(Storage.exists(PATH));

  Storage.failNextRenameFrom("/.crosspoint/global_stats.bin.tmp");
  const auto writeUncertain = GlobalReadingStats::load();
  EXPECT_FALSE(writeUncertain.writable);
  EXPECT_EQ(writeUncertain.totalReadingSeconds, 720U);
  EXPECT_FALSE(Storage.exists(PATH));
  EXPECT_EQ(GlobalReadingStats::load().totalReadingSeconds, 720U);
  EXPECT_TRUE(GlobalReadingStats::loadReadOnly().writable);
}

TEST_F(GlobalReadingStatsTest, KeepsCrossInkRecoveryAndForeignSnapshotsSeparate) {
  GlobalReadingStats source;
  source.totalReadingSeconds = 500;
  writeBytes("/.crosspoint/global_stats_crossink-aabbccddeeff.bin", crossInkSnapshot(source));
  const auto absent = GlobalReadingStats::load();
  EXPECT_TRUE(absent.writable);
  EXPECT_EQ(absent.totalReadingSeconds, 0U);
  EXPECT_FALSE(Storage.exists(PATH));

  writeBytes(CROSSINK, crossInkSnapshot(source));
  writeBytes("/.crosspoint/global_stats_crossink-001122334455.bin.recovery", {1});
  const auto recovering = GlobalReadingStats::load();
  EXPECT_FALSE(recovering.writable);
  EXPECT_FALSE(recovering.save());
  EXPECT_FALSE(Storage.exists(PATH));
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

TEST_F(GlobalReadingStatsTest, AggregationReadsCrossInkV4PeersAndSkipsTheLocalSnapshot) {
  ASSERT_TRUE(Storage.mkdir("/.crosspoint/synced_stats"));
  constexpr char peerPath[] = "/.crosspoint/synced_stats/device_aabbccddeeff.bin";
  GlobalReadingStats peer;
  peer.totalSessions = 4;
  peer.totalReadingSeconds = 600;
  peer.timeOfDaySeconds[1] = 600;
  peer.dayOfWeekSeconds[3] = 600;
  peer.readingHistoryAnchorDay = 1234;
  peer.readingHistoryBits[0] = 1;
  const auto snapshot = crossInkSnapshot(peer);
  writeBytes(peerPath, snapshot);
  writeBytes("/.crosspoint/synced_stats/device_001122334455.bin", snapshot);
  GlobalReadingStats local;
  local.totalSessions = 2;
  local.totalReadingSeconds = 30;

  const auto total = GlobalReadingStats::loadAggregated(local);
  EXPECT_TRUE(total.complete);
  EXPECT_EQ(total.sources, 2U);
  EXPECT_EQ(total.skipped, 0U);
  EXPECT_EQ(total.stats.totalSessions, 6U);
  EXPECT_EQ(total.stats.totalReadingSeconds, 630U);
  EXPECT_EQ(total.stats.timeOfDaySeconds[1], 600U);
  EXPECT_EQ(total.stats.dayOfWeekSeconds[3], 600U);
  EXPECT_EQ(total.stats.readingHistoryAnchorDay, peer.readingHistoryAnchorDay);
  EXPECT_EQ(total.stats.readingHistoryBits, peer.readingHistoryBits);

  HalFile file;
  ASSERT_TRUE(Storage.openFileForRead("TEST", peerPath, file));
  std::vector<uint8_t> stored(snapshot.size());
  ASSERT_EQ(file.read(stored.data(), stored.size()), static_cast<int>(stored.size()));
  ASSERT_TRUE(file.close());
  EXPECT_EQ(stored, snapshot);
  EXPECT_FALSE(Storage.exists(PATH));

  writeBytes(PATH, snapshot);
  EXPECT_FALSE(GlobalReadingStats::load().writable);
}

TEST_F(GlobalReadingStatsTest, AggregationRequiresTheCompleteCrossInkV4Snapshot) {
  ASSERT_TRUE(Storage.mkdir("/.crosspoint/synced_stats"));
  constexpr char peerPath[] = "/.crosspoint/synced_stats/device_aabbccddeeff.bin";
  const auto valid = crossInkSnapshot(GlobalReadingStats{});
  for (const int sizeDelta : {-1, 1}) {
    auto malformed = valid;
    malformed.resize(valid.size() + sizeDelta);
    writeBytes(peerPath, malformed);
    const auto total = GlobalReadingStats::loadAggregated(GlobalReadingStats{});
    EXPECT_FALSE(total.complete);
    EXPECT_EQ(total.sources, 1U);
    EXPECT_EQ(total.skipped, 1U);
  }
  writeBytes(peerPath, valid);
  Storage.failNextReadFor(peerPath);
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
