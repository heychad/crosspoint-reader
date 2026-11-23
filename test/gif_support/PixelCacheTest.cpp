#include <Epub/Epub/converters/PixelCache.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
class PixelCacheTest : public ::testing::Test {
 protected:
  std::string path;
  void SetUp() override {
    path = (std::filesystem::temp_directory_path() /
            (std::string("crosspoint-pixel-cache-") + ::testing::UnitTest::GetInstance()->current_test_info()->name()))
               .string();
    HalFileTest::failNextWrite = false;
    HalFileTest::failNextClose = false;
    HalFileTest::writeCalls = 0;
  }
  void TearDown() override {
    HalFileTest::failNextWrite = false;
    HalFileTest::failNextClose = false;
    Storage.remove(path);
  }
  std::vector<uint8_t> readCache() const {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  }
};
TEST_F(PixelCacheTest, PublishesCompleteCache) {
  PixelCache cache;
  ASSERT_TRUE(cache.begin(path, 4, 1, 0, 0, 1));
  EXPECT_TRUE(cache.finalize());
  EXPECT_TRUE(Storage.exists(path.c_str()));
  EXPECT_EQ(std::filesystem::file_size(path), 5u);
}
TEST_F(PixelCacheTest, RejectsFinalWriteFailure) {
  PixelCache cache;
  ASSERT_TRUE(cache.begin(path, 4, 1, 0, 0, 1));
  HalFileTest::failNextWrite = true;
  EXPECT_FALSE(cache.finalize());
  EXPECT_FALSE(Storage.exists(path.c_str()));
}
TEST_F(PixelCacheTest, RejectsCloseFailure) {
  PixelCache cache;
  ASSERT_TRUE(cache.begin(path, 4, 1, 0, 0, 1));
  HalFileTest::failNextClose = true;
  EXPECT_FALSE(cache.finalize());
  EXPECT_FALSE(Storage.exists(path.c_str()));
}

TEST_F(PixelCacheTest, BatchesScanlinesWithoutChangingPackedBytes) {
  PixelCache cache;
  ASSERT_TRUE(cache.begin(path, 9, 600, 0, 0, 1));
  std::vector<uint8_t> expected{9, 0, 0x58, 2};
  for (int row = 0; row < 600; ++row) {
    ASSERT_TRUE(cache.advanceTo(row));
    const auto value = static_cast<uint8_t>(row % 251);
    std::memset(cache.buffer + (row - cache.bandStart) * cache.bytesPerRow, value, cache.bytesPerRow);
    expected.insert(expected.end(), 3, value);
  }
  ASSERT_TRUE(cache.finalize());
  EXPECT_EQ(readCache(), expected);
  EXPECT_EQ(HalFileTest::writeCalls, 40u);
}

TEST_F(PixelCacheTest, RetainsBothColumnsOfEachDecoderBlockAndFillsClippedGaps) {
  PixelCache cache;
  ASSERT_TRUE(cache.begin(path, 8, 70, 0, 0, 16));
  ASSERT_TRUE(cache.advanceTo(1, 16));
  for (int row = 1; row < 17; ++row) cache.buffer[(row - cache.bandStart) * 2] = 0x55;
  ASSERT_TRUE(cache.advanceTo(1, 16));
  for (int row = 1; row < 17; ++row) cache.buffer[(row - cache.bandStart) * 2 + 1] = 0xAA;
  ASSERT_TRUE(cache.advanceTo(17, 16));
  for (int row = 17; row < 33; ++row) cache.buffer[(row - cache.bandStart) * 2] = 0xFF;
  ASSERT_TRUE(cache.advanceTo(68));
  cache.buffer[(68 - cache.bandStart) * 2 + 1] = 0x11;
  ASSERT_TRUE(cache.finalize());

  std::vector<uint8_t> expected(4 + 70 * 2, 0);
  expected[0] = 8;
  expected[2] = 70;
  for (int row = 1; row < 17; ++row) {
    expected[4 + row * 2] = 0x55;
    expected[4 + row * 2 + 1] = 0xAA;
  }
  for (int row = 17; row < 33; ++row) expected[4 + row * 2] = 0xFF;
  expected[4 + 68 * 2 + 1] = 0x11;
  EXPECT_EQ(readCache(), expected);
  EXPECT_EQ(HalFileTest::writeCalls, 7u);
}

TEST_F(PixelCacheTest, RemovesPartialBandAfterShortWrite) {
  PixelCache cache;
  ASSERT_TRUE(cache.begin(path, 8, 17, 0, 0, 1));
  cache.file.limitWritesTo(3);
  EXPECT_FALSE(cache.advanceTo(16));
  EXPECT_FALSE(cache.finalize());
  EXPECT_FALSE(Storage.exists(path.c_str()));
}

TEST_F(PixelCacheTest, CachesImageShorterThanTheDecoderBlockLimit) {
  PixelCache cache;
  ASSERT_TRUE(cache.begin(path, 8, 1, 0, 0, 16));
  ASSERT_TRUE(cache.advanceTo(0, 1));
  cache.buffer[1] = 0xAA;
  ASSERT_TRUE(cache.finalize());
  EXPECT_EQ(readCache(), (std::vector<uint8_t>{8, 0, 1, 0, 0, 0xAA}));
}
}  // namespace
