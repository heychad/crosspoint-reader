#include <Bitmap.h>
#include <BmpToBmpConverter.h>
#include <HalStorage.h>
#include <Print.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class Output final : public Print {
 public:
  std::vector<uint8_t> bytes;
  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* data, size_t count) override {
    bytes.insert(bytes.end(), data, data + count);
    return count;
  }
};

uint32_t le32(const std::vector<uint8_t>& b, size_t at) {
  return uint32_t{b[at]} | uint32_t{b[at + 1]} << 8 | uint32_t{b[at + 2]} << 16 | uint32_t{b[at + 3]} << 24;
}

void put32(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

// 24bpp or 1bpp BMP whose top (or left) half is black and the other half white.
std::vector<uint8_t> splitBmp(int width, int height, bool topDown, bool oneBit, bool leftRight = false) {
  const int bits = oneBit ? 1 : 24;
  const int rowBytes = (width * bits + 31) / 32 * 4;
  const uint32_t offset = 54 + (oneBit ? 8 : 0);
  std::vector<uint8_t> b = {'B', 'M'};
  put32(b, offset + rowBytes * height);
  put32(b, 0);
  put32(b, offset);
  put32(b, 40);
  put32(b, width);
  put32(b, topDown ? static_cast<uint32_t>(-height) : height);
  b.insert(b.end(), {1, 0, static_cast<uint8_t>(bits), 0});
  for (int i = 0; i < 6; ++i) put32(b, i == 4 && oneBit ? 2 : 0);
  if (oneBit) {
    put32(b, 0);
    put32(b, 0xFFFFFF);
  }
  for (int i = 0; i < height; ++i) {
    const int y = topDown ? i : height - 1 - i;
    if (!leftRight) {
      b.insert(b.end(), rowBytes, y < height / 2 ? 0 : 0xFF);
      continue;
    }
    for (int x = 0; x < rowBytes; ++x) b.push_back(x < rowBytes / 2 ? 0 : 0xFF);
  }
  return b;
}

class BmpToBmpTest : public ::testing::Test {
 protected:
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      (std::string("crosspoint-bmp-to-bmp-") + ::testing::UnitTest::GetInstance()->current_test_info()->name());
  HalFile file;
  void TearDown() override {
    file.close();
    std::filesystem::remove(path);
  }
  Output convert(const std::vector<uint8_t>& bmp, bool crop, int width = 40, int height = 64) {
    {
      std::ofstream out(path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(bmp.data()), bmp.size());
    }
    EXPECT_TRUE(file.open(path.c_str(), "rb"));
    Output out;
    EXPECT_TRUE(BmpToBmpConverter::bmpFileToBmpStream(file, out, width, height, crop, false));
    file.close();
    return out;
  }
  BmpReaderError parse(const std::vector<uint8_t>& bmp) {
    {
      std::ofstream out(path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(bmp.data()), bmp.size());
    }
    if (!file.open(path.c_str(), "rb")) return BmpReaderError::FileInvalid;
    Bitmap bitmap(file);
    const auto result = bitmap.parseHeaders();
    file.close();
    return result;
  }
  // Display stub is 64x40, so portrait output is at most 40x64. Returns level of pixel (x, y).
  static int level(const Output& out, int x, int y) {
    const unsigned bits = out.bytes[28];
    const uint32_t width = le32(out.bytes, 18);
    const auto rawHeight = static_cast<int32_t>(le32(out.bytes, 22));
    if (rawHeight > 0) y = rawHeight - 1 - y;
    const size_t row = le32(out.bytes, 10) + y * ((width * bits + 31) / 32 * 4);
    const uint8_t byte = out.bytes[row + x * bits / 8];
    return bits == 1 ? (byte >> (7 - x % 8)) & 1 : (byte >> (6 - (x % 4) * 2)) & 3;
  }
};

TEST_F(BmpToBmpTest, UpscalesSmallImageToFillScreen) {
  for (const bool topDown : {false, true}) {
    const Output out = convert(splitBmp(10, 16, topDown, false), true);
    EXPECT_EQ(le32(out.bytes, 18), 40u);
    EXPECT_EQ(le32(out.bytes, 22), topDown ? static_cast<uint32_t>(-64) : 64u);
    EXPECT_EQ(out.bytes[28], 2);
    EXPECT_EQ(out.bytes.size(), le32(out.bytes, 2));
    EXPECT_EQ(level(out, 20, 2), 0);
    EXPECT_EQ(level(out, 20, 61), 3);
  }
}

TEST_F(BmpToBmpTest, DownscalesAndFits) {
  const Output out = convert(splitBmp(160, 128, false, false), false);
  EXPECT_EQ(le32(out.bytes, 18), 40u);
  EXPECT_EQ(le32(out.bytes, 22), 32u);
  EXPECT_EQ(level(out, 5, 1), 0);
  EXPECT_EQ(level(out, 5, 30), 3);
}

TEST_F(BmpToBmpTest, CropsCenterWhileDownscaling) {
  for (const bool topDown : {false, true}) {
    const Output out = convert(splitBmp(320, 128, topDown, false, true), true);
    EXPECT_EQ(le32(out.bytes, 18), 40u);
    EXPECT_EQ(le32(out.bytes, 22), topDown ? static_cast<uint32_t>(-64) : 64u);
    EXPECT_EQ(level(out, 2, 32), 0);
    EXPECT_EQ(level(out, 37, 32), 3);
  }
}

TEST_F(BmpToBmpTest, OneBitSourceStaysOneBit) {
  const Output out = convert(splitBmp(20, 32, false, true), true);
  EXPECT_EQ(out.bytes[28], 1);
  EXPECT_EQ(le32(out.bytes, 18), 40u);
  EXPECT_EQ(level(out, 3, 2), 0);
  EXPECT_EQ(level(out, 3, 61), 1);
}

TEST_F(BmpToBmpTest, CropUsesExactRuntimeBoundsForBothOrientations) {
  for (const auto target : {std::pair{480, 800}, std::pair{800, 480}}) {
    for (const auto source : {std::pair{12, 8}, std::pair{320, 128}}) {
      for (const bool topDown : {false, true}) {
        const Output out =
            convert(splitBmp(source.first, source.second, topDown, false), true, target.first, target.second);
        EXPECT_EQ(le32(out.bytes, 18), target.first);
        EXPECT_EQ(static_cast<int32_t>(le32(out.bytes, 22)), topDown ? -target.second : target.second);
        EXPECT_EQ(level(out, target.first / 2, 0), 0);
        EXPECT_EQ(level(out, target.first / 2, target.second - 1), 3);
      }
    }
  }
}

TEST_F(BmpToBmpTest, FitUsesRuntimeAspectRatioAndCentersTallCrop) {
  const auto fitted = convert(splitBmp(80, 80, true, false), false, 800, 480);
  EXPECT_EQ(le32(fitted.bytes, 18), 480u);
  EXPECT_EQ(static_cast<int32_t>(le32(fitted.bytes, 22)), -480);
  auto tall = splitBmp(40, 160, true, false);
  std::fill(tall.begin() + 54 + 120 * 40, tall.end(), 0xFF);
  const auto cropped = convert(tall, true, 40, 40);
  for (int y = 0; y < 40; ++y) EXPECT_EQ(level(cropped, 20, y), 3);
}

TEST_F(BmpToBmpTest, PreservesNativeGrayLevelsWhenResizing) {
  std::vector<uint8_t> bytes = {'B', 'M'};
  put32(bytes, 70 + 16 * 16);
  put32(bytes, 0);
  put32(bytes, 70);
  put32(bytes, 40);
  put32(bytes, 64);
  put32(bytes, 16);
  bytes.insert(bytes.end(), {1, 0, 2, 0});
  for (int i = 0; i < 6; ++i) put32(bytes, i == 4 ? 4 : 0);
  for (int level = 0; level < 4; ++level) put32(bytes, level * 0x555555);
  bytes.insert(bytes.end(), 16 * 16, 0x55);
  const auto out = convert(bytes, true, 128, 32);
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 128; ++x) EXPECT_EQ(level(out, x, y), 1);
}

TEST_F(BmpToBmpTest, RejectsInvalidBoundsAndTruncatedSourceRows) {
  const auto bytes = splitBmp(20, 32, false, false);
  {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  for (const auto bounds : {std::pair{0, 64}, std::pair{40, -1}, std::pair{3073, 64}}) {
    ASSERT_TRUE(file.open(path.c_str(), "rb"));
    Output out;
    EXPECT_FALSE(BmpToBmpConverter::bmpFileToBmpStream(file, out, bounds.first, bounds.second, true, false));
    EXPECT_TRUE(out.bytes.empty());
  }
  file.close();
  std::filesystem::resize_file(path, bytes.size() - 1);
  ASSERT_TRUE(file.open(path.c_str(), "rb"));
  Output out;
  EXPECT_FALSE(BmpToBmpConverter::bmpFileToBmpStream(file, out, 40, 64, true, false));
}

TEST_F(BmpToBmpTest, PropagatesShortOutputWrites) {
  class ShortOutput final : public Print {
   public:
    size_t remaining = 90;
    size_t write(uint8_t) override { return remaining ? (--remaining, 1) : 0; }
  } out;
  const auto bytes = splitBmp(20, 32, false, false);
  {
    std::ofstream source(path, std::ios::binary);
    source.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  ASSERT_TRUE(file.open(path.c_str(), "rb"));
  EXPECT_FALSE(BmpToBmpConverter::bmpFileToBmpStream(file, out, 40, 64, true, false));
}

TEST_F(BmpToBmpTest, RejectsMinimumSignedHeightAndTruncatedPalette) {
  auto valid = splitBmp(20, 32, false, true);
  EXPECT_EQ(parse(valid), BmpReaderError::Ok);
  auto invalidHeight = valid;
  invalidHeight[22] = 0;
  invalidHeight[23] = 0;
  invalidHeight[24] = 0;
  invalidHeight[25] = 0x80;
  EXPECT_EQ(parse(invalidHeight), BmpReaderError::BadDimensions);
  auto shortPalette = valid;
  shortPalette.resize(59);
  EXPECT_EQ(parse(shortPalette), BmpReaderError::ShortReadPalette);
}

}  // namespace
