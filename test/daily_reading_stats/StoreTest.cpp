#include <HalStorage.h>
#include <Memory.h>
#include <gtest/gtest.h>

#include "activities/reader/DailyReadingStatsStore.h"
#include "activities/reader/ReadingStatsUtils.h"

using namespace dailyreading;
namespace {
const char* A = "/.crosspoint/daily_reading/2026-11.a.bin";
const char* B = "/.crosspoint/daily_reading/2026-11.b.bin";
uint32_t day = readingStatsDayIndex({2026, 11, 15});
void put(const std::string& path, const std::string& bytes) {
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", path, file));
  ASSERT_EQ(file.write(bytes.data(), bytes.size()), bytes.size());
  ASSERT_TRUE(file.close());
}
std::string bytes(const char* path) {
  HalFile file;
  if (!Storage.openFileForRead("TEST", path, file)) return {};
  std::string result(file.fileSize(), '\0');
  EXPECT_EQ(file.read(result.data(), result.size()), static_cast<int>(result.size()));
  EXPECT_TRUE(file.close());
  return result;
}
std::string legacyPath(uint32_t value) {
  char path[64];
  snprintf(path, sizeof(path), "%s/%05lu.bin", DIRECTORY, static_cast<unsigned long>(value));
  return path;
}
std::string legacy(uint32_t value, Counter counter) {
  std::string result(LEGACY_BYTES, '\0');
  auto* p = reinterpret_cast<uint8_t*>(result.data());
  p[0] = 1;
  write32(p + 1, counter.seconds);
  write32(p + 5, counter.uploaded);
  write32(p + 9, checksum(p, 9, 2166136261U ^ value));
  return result;
}
class DailyStore : public testing::Test {
  void SetUp() override {
    Storage.reset();
    failNextBackupAllocation = false;
  }
};
TEST_F(DailyStore, ReadsDoNotCreateFilesAndAbsoluteWritesRoundTrip) {
  Counter value{1, 1};
  EXPECT_EQ(read(day, value), ReadResult::Missing);
  EXPECT_EQ(value, Counter{});
  EXPECT_FALSE(Storage.exists(DIRECTORY));
  ASSERT_TRUE(write(day, {120, 0}));
  ASSERT_EQ(read(day, value), ReadResult::Available);
  EXPECT_EQ(value, (Counter{120, 0}));
  const auto before = bytes(A), other = bytes(B);
  EXPECT_TRUE(write(day, {120, 0}));
  EXPECT_EQ(bytes(A), before);
  EXPECT_EQ(bytes(B), other);
  ASSERT_TRUE(write(day + 1, {40, 0}));
  ASSERT_EQ(read(day, value), ReadResult::Available);
  EXPECT_EQ(value.seconds, 120U);
  ASSERT_EQ(read(day + 1, value), ReadResult::Available);
  EXPECT_EQ(value.seconds, 40U);
}
TEST_F(DailyStore, RejectsDecreasesAndInvalidCounters) {
  ASSERT_TRUE(write(day, {120, 90}));
  const auto first = bytes(A), second = bytes(B);
  EXPECT_FALSE(write(day, {119, 90}));
  EXPECT_FALSE(write(day, {120, 89}));
  EXPECT_FALSE(write(day, {120, 121}));
  EXPECT_EQ(bytes(A), first);
  EXPECT_EQ(bytes(B), second);
}
TEST_F(DailyStore, PreservesAndMergesLegacySiblingsAndOtherDays) {
  const auto path = legacyPath(day), neighbor = legacyPath(day + 1);
  const auto one = legacy(day, {100, 30}), two = legacy(day, {140, 20});
  put(path, one);
  put(path + ".bak", two);
  put(path + ".tmp", std::string("\1torn", 5));
  put(neighbor, legacy(day + 1, {55, 10}));
  Counter value;
  ASSERT_EQ(read(day, value), ReadResult::Available);
  EXPECT_EQ(value, (Counter{140, 30}));
  EXPECT_FALSE(Storage.exists(A));
  ASSERT_TRUE(write(day, {160, 30}));
  EXPECT_EQ(bytes(path.c_str()), one);
  EXPECT_EQ(bytes((path + ".bak").c_str()), two);
  EXPECT_EQ(bytes((path + ".tmp").c_str()), std::string("\1torn", 5));
  ASSERT_EQ(read(day + 1, value), ReadResult::Available);
  EXPECT_EQ(value, (Counter{55, 10}));
}
TEST_F(DailyStore, EveryTornTargetRetainsPreviousCounter) {
  for (size_t cut = 0; cut < MONTH_BYTES; ++cut) {
    SCOPED_TRACE(cut);
    Storage.reset();
    ASSERT_TRUE(write(day, {120, 0}));
    const auto survivor = bytes(A);
    Storage.failNextWriteFor(B, cut);
    EXPECT_FALSE(write(day, {200, 0}));
    EXPECT_EQ(bytes(A), survivor);
    Counter value;
    ASSERT_EQ(read(day, value), ReadResult::Available);
    EXPECT_EQ(value.seconds, 120U);
  }
}
TEST_F(DailyStore, UncertainSyncAndCloseCanRetrySameAbsoluteValue) {
  for (bool closeFailure : {false, true}) {
    Storage.reset();
    ASSERT_TRUE(write(day, {120, 0}));
    if (closeFailure)
      Storage.failNextNewCloseFor(B);
    else
      Storage.failNextNewSyncFor(B);
    EXPECT_FALSE(write(day, {200, 0}));
    EXPECT_TRUE(write(day, {200, 0}));
    Counter value;
    ASSERT_EQ(read(day, value), ReadResult::Available);
    EXPECT_EQ(value.seconds, 200U);
  }
}
TEST_F(DailyStore, RefusesUnknownAndUnreadableSlotsWithoutOverwriting) {
  ASSERT_TRUE(write(day, {120, 0}));
  const auto first = bytes(A);
  auto newer = bytes(B);
  newer[0] = 3;
  put(B, newer);
  EXPECT_FALSE(write(day, {200, 0}));
  EXPECT_EQ(bytes(B), newer);
  EXPECT_EQ(bytes(A), first);
  Counter value;
  EXPECT_EQ(read(day, value), ReadResult::Unavailable);
  put(B, std::string(MONTH_BYTES + 1, '\0'));
  EXPECT_FALSE(write(day, {200, 0}));
  put(B, first);
  Storage.failNextReadFor(B);
  EXPECT_FALSE(write(day, {200, 0}));
  EXPECT_EQ(bytes(A), first);
  Storage.failNextCloseFor(B);
  EXPECT_EQ(read(day, value), ReadResult::Unavailable);
}
TEST_F(DailyStore, SharedClustersBetweenSlotsOrLegacyArePreserved) {
  ASSERT_TRUE(write(day, {120, 0}));
  ASSERT_TRUE(Storage.remove(B));
  ASSERT_TRUE(Storage.aliasPath(A, B));
  const auto original = bytes(A);
  EXPECT_FALSE(write(day, {200, 0}));
  EXPECT_EQ(bytes(A), original);
  ASSERT_TRUE(Storage.remove(B));
  const auto path = legacyPath(day);
  const auto old = legacy(day, {60, 0});
  put(path, old);
  ASSERT_TRUE(Storage.aliasPath(path, B));
  EXPECT_FALSE(write(day, {200, 0}));
  EXPECT_EQ(bytes(A), original);
  EXPECT_EQ(bytes(path.c_str()), old);
}
TEST_F(DailyStore, CorruptOrNewerLegacyBlocksWritesWithoutChangingIt) {
  for (const auto& data : {std::string("bad"), std::string(14, '\0')}) {
    Storage.reset();
    const auto path = legacyPath(day);
    put(path, data);
    Counter value;
    EXPECT_EQ(read(day, value), ReadResult::Unavailable);
    EXPECT_FALSE(write(day, {100, 0}));
    EXPECT_EQ(bytes(path.c_str()), data);
    EXPECT_FALSE(Storage.exists(A));
  }
}
TEST_F(DailyStore, AllocationLockAndDateFailuresDoNotCreateFiles) {
  Counter value;
  failNextBackupAllocation = true;
  EXPECT_EQ(read(day, value), ReadResult::Unavailable);
  failNextBackupAllocation = true;
  EXPECT_FALSE(write(day, {1, 0}));
  Storage.failNextStorageLock();
  EXPECT_FALSE(write(day, {1, 0}));
  EXPECT_EQ(read(UINT32_MAX, value), ReadResult::Unavailable);
  EXPECT_FALSE(write(UINT32_MAX, {1, 0}));
  EXPECT_FALSE(Storage.exists(DIRECTORY));
}
TEST_F(DailyStore, MonthBoundariesAndLeapDaysStayIndependent) {
  const uint32_t leap = readingStatsDayIndex({2028, 2, 29});
  ASSERT_TRUE(write(leap, {60, 0}));
  ASSERT_TRUE(write(leap + 1, {120, 0}));
  Counter value;
  ASSERT_EQ(read(leap, value), ReadResult::Available);
  EXPECT_EQ(value.seconds, 60U);
  ASSERT_EQ(read(leap + 1, value), ReadResult::Available);
  EXPECT_EQ(value.seconds, 120U);
}
}  // namespace

TEST_F(DailyStore, InterruptedFirstBootstrapPreservesItsEvidence) {
  Storage.failNextWriteFor(B, 4);
  EXPECT_FALSE(write(day, {120, 0}));
  const auto torn = bytes(B);
  EXPECT_FALSE(torn.empty());
  EXPECT_FALSE(write(day, {120, 0}));
  Counter value;
  EXPECT_EQ(read(day, value), ReadResult::Unavailable);
  EXPECT_EQ(bytes(B), torn);
  EXPECT_FALSE(Storage.exists(A));
}
TEST_F(DailyStore, RejectsDirectoryAndRegularFileConfusion) {
  put(DIRECTORY, "not a directory");
  EXPECT_FALSE(write(day, {1, 0}));
  EXPECT_EQ(bytes(DIRECTORY), "not a directory");
  EXPECT_FALSE(Storage.exists(A));
}
