#include <HalStorage.h>
#include <StatsSafeReplace.h>
#include <gtest/gtest.h>

#include <array>
#include <string>

namespace {
constexpr char PATH[] = "/stats/data.bin";
using Bytes = std::array<uint8_t, 73>;
Bytes payload(uint8_t value) {
  Bytes data{};
  data.fill(value);
  return data;
}
bool writeFile(const std::string& path, const Bytes& data) {
  HalFile f;
  if (!Storage.openFileForWrite("TEST", path, f)) return false;
  const bool written = f.write(data.data(), data.size()) == data.size();
  return f.close() && written;
}
bool readFile(const std::string& path, Bytes& data) {
  HalFile f;
  if (!Storage.openFileForRead("TEST", path, f)) return false;
  const bool read = f.fileSize() == data.size() && f.read(data.data(), data.size()) == static_cast<int>(data.size());
  return f.close() && read;
}
}  // namespace

TEST(StatsSafeReplace, NormalSavesRetainOrRemoveBackupAsRequested) {
  for (bool keep : {false, true}) {
    Storage.reset();
    auto a = payload(1), b = payload(2), c = payload(3), actual = payload(0);
    ASSERT_TRUE(statsreplace::save("TEST", PATH, a.data(), a.size(), keep));
    ASSERT_TRUE(statsreplace::save("TEST", PATH, b.data(), b.size(), keep));
    EXPECT_EQ(Storage.exists((std::string(PATH) + ".bak").c_str()), keep);
    ASSERT_TRUE(statsreplace::save("TEST", PATH, c.data(), c.size(), keep));
    ASSERT_TRUE(readFile(PATH, actual));
    EXPECT_EQ(actual, c);
  }
}

TEST(StatsSafeReplace, RefusesAliasedPrimaryAndBackupWithoutWriting) {
  Storage.reset();
  const auto original = payload(1), update = payload(2);
  ASSERT_TRUE(writeFile(PATH, original));
  ASSERT_TRUE(Storage.aliasPath(PATH, std::string(PATH) + ".bak"));
  EXPECT_FALSE(statsreplace::save("TEST", PATH, update.data(), update.size(), true));
  Bytes actual{};
  ASSERT_TRUE(readFile(PATH, actual));
  EXPECT_EQ(actual, original);
  EXPECT_TRUE(Storage.exists((std::string(PATH) + ".bak").c_str()));
  EXPECT_FALSE(Storage.exists((std::string(PATH) + ".tmp").c_str()));
}

TEST(StatsSafeReplace, ReadCloseFailureDoesNotStartAReplacement) {
  Storage.reset();
  const auto original = payload(1), update = payload(2);
  ASSERT_TRUE(writeFile(PATH, original));
  Storage.failNextCloseFor(PATH);
  EXPECT_FALSE(statsreplace::save("TEST", PATH, update.data(), update.size(), true));
  Bytes actual{};
  ASSERT_TRUE(readFile(PATH, actual));
  EXPECT_EQ(actual, original);
  EXPECT_FALSE(Storage.exists((std::string(PATH) + ".tmp").c_str()));
  EXPECT_FALSE(Storage.exists((std::string(PATH) + ".bak").c_str()));
}

TEST(StatsSafeReplace, ReadFailureDoesNotStartAReplacement) {
  Storage.reset();
  const auto original = payload(1), update = payload(2);
  ASSERT_TRUE(writeFile(PATH, original));
  Storage.failNextReadFor(PATH);
  EXPECT_FALSE(statsreplace::save("TEST", PATH, update.data(), update.size(), true));
  Bytes actual{};
  ASSERT_TRUE(readFile(PATH, actual));
  EXPECT_EQ(actual, original);
  EXPECT_FALSE(Storage.exists((std::string(PATH) + ".tmp").c_str()));
  EXPECT_FALSE(Storage.exists((std::string(PATH) + ".bak").c_str()));
}

TEST(StatsSafeReplace, RefusesAliasedPrimaryAndTempWithoutWriting) {
  Storage.reset();
  const auto original = payload(1), update = payload(2);
  ASSERT_TRUE(writeFile(PATH, original));
  ASSERT_TRUE(Storage.aliasPath(PATH, std::string(PATH) + ".tmp"));
  EXPECT_FALSE(statsreplace::save("TEST", PATH, update.data(), update.size(), false));
  Bytes actual{};
  ASSERT_TRUE(readFile(PATH, actual));
  EXPECT_EQ(actual, original);
  EXPECT_TRUE(Storage.exists((std::string(PATH) + ".tmp").c_str()));
}

TEST(StatsSafeReplace, AliasedFailedPublishKeepsAllRecoveryFiles) {
  Storage.reset();
  const auto original = payload(1), update = payload(2);
  ASSERT_TRUE(writeFile(PATH, original));
  Storage.failNextAliasRenameFrom(std::string(PATH) + ".tmp");
  EXPECT_FALSE(statsreplace::save("TEST", PATH, update.data(), update.size(), true));
  Bytes old{}, pending{};
  ASSERT_TRUE(readFile(std::string(PATH) + ".bak", old));
  ASSERT_TRUE(readFile(std::string(PATH) + ".tmp", pending));
  EXPECT_EQ(old, original);
  EXPECT_EQ(pending, update);
  EXPECT_FALSE(statsreplace::save("TEST", PATH, update.data(), update.size(), true));
}

TEST(StatsSafeReplace, FailedTempSyncLeavesOriginalAndTempForReview) {
  Storage.reset();
  const auto original = payload(1), update = payload(2);
  ASSERT_TRUE(writeFile(PATH, original));
  Storage.failNextNewSyncFor(std::string(PATH) + ".tmp");
  EXPECT_FALSE(statsreplace::save("TEST", PATH, update.data(), update.size(), true));
  Bytes actual{};
  ASSERT_TRUE(readFile(PATH, actual));
  EXPECT_EQ(actual, original);
  EXPECT_TRUE(Storage.exists((std::string(PATH) + ".tmp").c_str()));
}

TEST(StatsSafeReplace, FailedPublishPreservesOldAndPendingBytesAfterRollback) {
  Storage.reset();
  const auto original = payload(1), update = payload(2);
  ASSERT_TRUE(writeFile(PATH, original));
  Storage.failNextRenameFrom(std::string(PATH) + ".tmp");
  EXPECT_FALSE(statsreplace::save("TEST", PATH, update.data(), update.size(), true));
  Bytes old{}, pending{};
  ASSERT_TRUE(readFile(PATH, old));
  ASSERT_TRUE(readFile(std::string(PATH) + ".tmp", pending));
  EXPECT_EQ(old, original);
  EXPECT_EQ(pending, update);
}

TEST(StatsSafeReplace, AliasedFailedRotationNeverDeletesEitherName) {
  Storage.reset();
  const auto original = payload(1), update = payload(2);
  ASSERT_TRUE(writeFile(PATH, original));
  Storage.failNextAliasRenameFrom(PATH);
  EXPECT_FALSE(statsreplace::save("TEST", PATH, update.data(), update.size(), true));
  Bytes primary{}, backup{}, staged{};
  ASSERT_TRUE(readFile(PATH, primary));
  ASSERT_TRUE(readFile(std::string(PATH) + ".bak", backup));
  ASSERT_TRUE(readFile(std::string(PATH) + ".tmp", staged));
  EXPECT_EQ(primary, original);
  EXPECT_EQ(backup, original);
  EXPECT_EQ(staged, update);
  EXPECT_FALSE(statsreplace::save("TEST", PATH, update.data(), update.size(), true));
  EXPECT_TRUE(Storage.exists(PATH));
  EXPECT_TRUE(Storage.exists((std::string(PATH) + ".bak").c_str()));
  EXPECT_TRUE(Storage.exists((std::string(PATH) + ".tmp").c_str()));
}

TEST(StatsSafeReplace, RetryPromotesOnlyExactOrphanTemp) {
  Storage.reset();
  const auto expected = payload(4), other = payload(5);
  ASSERT_TRUE(writeFile(std::string(PATH) + ".tmp", expected));
  EXPECT_FALSE(statsreplace::save("TEST", PATH, other.data(), other.size(), true));
  EXPECT_FALSE(Storage.exists(PATH));
  EXPECT_TRUE(statsreplace::save("TEST", PATH, expected.data(), expected.size(), true));
  Bytes result{};
  ASSERT_TRUE(readFile(PATH, result));
  EXPECT_EQ(result, expected);
}
