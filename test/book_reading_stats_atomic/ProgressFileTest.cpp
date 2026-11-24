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
