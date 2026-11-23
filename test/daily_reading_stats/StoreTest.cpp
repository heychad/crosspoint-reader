#include <HalStorage.h>
#include <Memory.h>
#include <gtest/gtest.h>

#include "activities/reader/DailyReadingStatsStore.h"
#include "activities/reader/ReadingStatsUtils.h"

using namespace dailyreading;
namespace {
const char* A = "/.crosspoint/daily_reading/2026-11.a.bin";
const char* B = "/.crosspoint/daily_reading/2026-11.b.bin";
const char* CROSSINK = "/.crosspoint/daily_reading_crossink-001122334455";
const char* CROSSINK_A = "/.crosspoint/daily_reading_crossink-001122334455/2026-11.a.bin";
const char* CROSSINK_B = "/.crosspoint/daily_reading_crossink-001122334455/2026-11.b.bin";
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
std::string legacyPath(uint32_t value, const char* directory = DIRECTORY) {
  char path[64];
  snprintf(path, sizeof(path), "%s/%05lu.bin", directory, static_cast<unsigned long>(value));
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
std::string november(const Month& month) {
  std::array<uint8_t, MONTH_BYTES> encoded{};
  EXPECT_TRUE(encodeMonth(month, day - 14, 30, encoded));
  return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
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
TEST_F(DailyStore, SeedsNativeMonthFromNewestCrossInkSlotAndKeepsBothSourceFiles) {
  Month old, latest;
  old.generation = UINT32_MAX;
  old.days[14] = {80, 30};
  latest.days[14] = {150, 60};
  latest.days[15] = {50, 40};
  const auto first = november(old), second = november(latest);
  put(CROSSINK_A, first);
  put(CROSSINK_B, second);
  const auto qualifiedLegacy = legacyPath(day + 2, CROSSINK);
  put(qualifiedLegacy, legacy(day + 2, {180, 90}));

  Counter value;
  ASSERT_EQ(read(day, value), ReadResult::Available);
  EXPECT_EQ(value, (Counter{150, 60}));
  EXPECT_FALSE(Storage.exists(DIRECTORY));
  ASSERT_TRUE(write(day, {170, 60}));
  EXPECT_EQ(bytes(CROSSINK_A), first);
  EXPECT_EQ(bytes(CROSSINK_B), second);
  EXPECT_EQ(bytes(qualifiedLegacy.c_str()), legacy(day + 2, {180, 90}));
  ASSERT_EQ(read(day + 1, value), ReadResult::Available);
  EXPECT_EQ(value, (Counter{50, 40}));
  ASSERT_EQ(read(day + 2, value), ReadResult::Available);
  EXPECT_EQ(value, (Counter{180, 90}));

  // An independently updated CrossInk folder must not override native history.
  latest.days[14] = {231, 100};
  put(CROSSINK_B, november(latest));
  ASSERT_EQ(read(day, value), ReadResult::Available);
  EXPECT_EQ(value, (Counter{170, 60}));
}
TEST_F(DailyStore, CheckedCrossInkMonthSurvivesInterruptedNativeBootstrap) {
  Month month;
  month.days[14] = {90, 50};
  const auto snapshot = november(month);
  put(CROSSINK_A, snapshot);
  Storage.failNextWriteFor(B, 12);
  EXPECT_FALSE(write(day, {120, 50}));
  EXPECT_FALSE(Storage.exists(A));
  ASSERT_TRUE(Storage.exists(B));
  EXPECT_EQ(bytes(CROSSINK_A), snapshot);
  Counter value;
  ASSERT_EQ(read(day, value), ReadResult::Available);
  EXPECT_EQ(value, (Counter{90, 50}));
  ASSERT_TRUE(write(day, {120, 50}));
  ASSERT_EQ(read(day, value), ReadResult::Available);
  EXPECT_EQ(value, (Counter{120, 50}));
}
TEST_F(DailyStore, IgnoresForeignFoldersAndBlocksImportOnUncertainSource) {
  Month month;
  month.days[14] = {45, 20};
  const auto snapshot = november(month);
  put("/.crosspoint/daily_reading_crossink-aabbccddeeff/2026-11.a.bin", snapshot);
  Counter value;
  EXPECT_EQ(read(day, value), ReadResult::Missing);
  put(CROSSINK_A, snapshot);
  Storage.failReadsFor(CROSSINK_B);
  EXPECT_EQ(read(day, value), ReadResult::Unavailable);
  EXPECT_FALSE(write(day, {75, 20}));
  EXPECT_FALSE(Storage.exists(DIRECTORY));
  EXPECT_EQ(bytes(CROSSINK_A), snapshot);
}
TEST_F(DailyStore, SeedsFromQualifiedLegacyAndNativeLegacyWithoutAddingOverlappingTotals) {
  const auto qualified = legacyPath(day, CROSSINK);
  const auto source = legacy(day, {100, 30});
  put(qualified, source);
  put(legacyPath(day), legacy(day, {125, 20}));
  Counter value;
  ASSERT_EQ(read(day, value), ReadResult::Available);
  EXPECT_EQ(value, (Counter{125, 30}));
  ASSERT_TRUE(write(day, {150, 30}));
  EXPECT_EQ(bytes(qualified.c_str()), source);
  ASSERT_EQ(read(day, value), ReadResult::Available);
  EXPECT_EQ(value, (Counter{150, 30}));
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
    ASSERT_TRUE(write(day, {200, 0}));
    ASSERT_EQ(read(day, value), ReadResult::Available);
    EXPECT_EQ(value.seconds, 200U);
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

TEST_F(DailyStore, EveryInterruptedFirstBootstrapCanRetryWithoutDeletingLegacy) {
  for (size_t cut = 0; cut < MONTH_BYTES; ++cut) {
    SCOPED_TRACE(cut);
    Storage.reset();
    const auto path = legacyPath(day);
    const auto old = legacy(day, {60, 0});
    put(path, old);
    Storage.failNextWriteFor(B, cut);
    EXPECT_FALSE(write(day, {120, 0}));
    Counter value;
    EXPECT_EQ(read(day, value), ReadResult::Unavailable);
    ASSERT_TRUE(write(day, {120, 0}));
    ASSERT_EQ(read(day, value), ReadResult::Available);
    EXPECT_EQ(value.seconds, 120U);
    EXPECT_EQ(bytes(path.c_str()), old);
  }
}
TEST_F(DailyStore, BootstrapRefusesForeignUnreadableAndAliasedEvidence) {
  const auto old = legacy(day, {60, 0}), path = legacyPath(day);
  put(path, old);
  ASSERT_TRUE(Storage.aliasPath(path, B));
  EXPECT_FALSE(write(day, {120, 0}));
  EXPECT_EQ(bytes(path.c_str()), old);
  EXPECT_FALSE(Storage.exists(A));
  Storage.reset();
  put(B, std::string(1, char(3)));
  const auto newer = bytes(B);
  EXPECT_FALSE(write(day, {120, 0}));
  EXPECT_EQ(bytes(B), newer);
  Storage.reset();
  put(B, std::string(1, char(2)));
  Storage.failNextReadFor(B);
  EXPECT_FALSE(write(day, {120, 0}));
  EXPECT_FALSE(Storage.exists(A));
  Storage.reset();
  put(A, std::string(1, char(2)));
  EXPECT_FALSE(write(day, {120, 0}));
  EXPECT_FALSE(Storage.exists(B));
}

TEST_F(DailyStore, RejectsDirectoryAndRegularFileConfusion) {
  put(DIRECTORY, "not a directory");
  EXPECT_FALSE(write(day, {1, 0}));
  EXPECT_EQ(bytes(DIRECTORY), "not a directory");
  EXPECT_FALSE(Storage.exists(A));
}
