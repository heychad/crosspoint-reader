#include <HalStorage.h>
#include <ProgressFile.h>
#include <gtest/gtest.h>

#include <array>

TEST(ProgressFile, FailedTempWritesKeepThePriorPositionAndCanBeRetried) {
  constexpr std::array<uint8_t, 6> prior{1, 0, 72, 0, 99, 0};
  constexpr std::array<uint8_t, 6> next{1, 0, 73, 0, 99, 0};
  constexpr char cache[] = "/book";
  constexpr char temp[] = "/book/progress.bin.tmp";
  auto readPosition = [] {
    std::array<uint8_t, 6> position{};
    HalFile file;
    EXPECT_TRUE(Storage.openFileForRead("TEST", "/book/progress.bin", file));
    if (file) {
      EXPECT_EQ(file.fileSize(), position.size());
      EXPECT_EQ(file.read(position.data(), position.size()), static_cast<int>(position.size()));
      EXPECT_TRUE(file.close());
    }
    return position;
  };

  struct Fault {
    const char* name;
    void (*inject)(const char*);
  };
  const Fault faults[] = {
      {"short write", [](const char* path) { Storage.failNextWriteFor(path, 2); }},
      {"sync", [](const char* path) { Storage.failNextNewSyncFor(path); }},
      {"sticky write error", [](const char* path) { Storage.setNextWriteErrorFor(path); }},
      {"close", [](const char* path) { Storage.failNextNewCloseFor(path); }},
  };
  for (const auto& fault : faults) {
    SCOPED_TRACE(fault.name);
    Storage.reset();
    ASSERT_TRUE(ProgressFile::writeAtomic(cache, prior.data(), prior.size()));
    fault.inject(temp);
    ASSERT_FALSE(ProgressFile::writeAtomic(cache, next.data(), next.size()));
    ASSERT_EQ(readPosition(), prior);
    ASSERT_TRUE(ProgressFile::writeAtomic(cache, next.data(), next.size()));
    EXPECT_EQ(readPosition(), next);
  }
}

TEST(ProgressFile, StaleTempSharingPrimaryOrBackupIsPreserved) {
  constexpr std::array<uint8_t, 6> prior{1, 0, 11, 0, 76, 0};
  constexpr std::array<uint8_t, 6> next{1, 0, 12, 0, 76, 0};
  for (const auto* source : {"/book/progress.bin", "/book/progress.bin.bak"}) {
    SCOPED_TRACE(source);
    Storage.reset();
    ASSERT_TRUE(ProgressFile::writeAtomic("/book", prior.data(), prior.size()));
    HalFile backup;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", "/book/progress.bin.bak", backup));
    ASSERT_EQ(backup.write(prior.data(), prior.size()), prior.size());
    ASSERT_TRUE(backup.close());
    ASSERT_TRUE(Storage.aliasPath(source, "/book/progress.bin.tmp"));
    EXPECT_FALSE(ProgressFile::writeAtomic("/book", next.data(), next.size()));
    for (const auto* name : {"/book/progress.bin", "/book/progress.bin.bak", "/book/progress.bin.tmp"}) {
      HalFile file;
      ASSERT_TRUE(Storage.openFileForRead("TEST", name, file));
      std::array<uint8_t, 6> position{};
      ASSERT_EQ(file.read(position.data(), position.size()), static_cast<int>(position.size()));
      EXPECT_EQ(position, prior);
      ASSERT_TRUE(file.close());
    }
  }
}

TEST(ProgressFile, StaleIndependentTempCanBeReplacedWithAShorterPosition) {
  constexpr std::array<uint8_t, 10> withOffset{3, 0, 16, 0, 99, 0, 0, 1, 0, 0};
  constexpr std::array<uint8_t, 6> withoutOffset{3, 0, 17, 0, 99, 0};
  Storage.reset();
  ASSERT_TRUE(ProgressFile::writeAtomic("/book", withOffset.data(), withOffset.size()));
  HalFile temp;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/book/progress.bin.tmp", temp));
  ASSERT_EQ(temp.write(withOffset.data(), withOffset.size()), withOffset.size());
  ASSERT_TRUE(temp.close());
  ASSERT_TRUE(ProgressFile::writeAtomic("/book", withoutOffset.data(), withoutOffset.size()));
  HalFile file;
  ASSERT_TRUE(Storage.openFileForRead("TEST", "/book/progress.bin", file));
  ASSERT_EQ(file.fileSize(), withoutOffset.size());
  std::array<uint8_t, 6> position{};
  ASSERT_EQ(file.read(position.data(), position.size()), static_cast<int>(position.size()));
  EXPECT_EQ(position, withoutOffset);
  EXPECT_TRUE(file.close());
}
