#include <Memory.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "network/sync/StageWriter.h"

namespace {
constexpr char NAME[] = "__xsync_0123456789abcdef0123456789abcdef.part";
constexpr char PATH[] = "/__xsync_0123456789abcdef0123456789abcdef.part";
class StageTest : public ::testing::Test {
 protected:
  HalStorage storage;
  StageWriter writer{storage};
  void SetUp() override {
    fake::files.clear();
    fake::failWrite = fake::failClose = failAllocation = false;
  }
};
TEST_F(StageTest, WritesExactBytesAndRefusesReplacement) {
  std::vector<uint8_t> input(5000, 42);
  ASSERT_TRUE(writer.begin(NAME, input.size()));
  ASSERT_TRUE(writer.write(input.data(), input.size()));
  ASSERT_TRUE(writer.finish());
  ASSERT_TRUE(writer.takeResult());
  ASSERT_EQ(fake::files.at(PATH)->bytes, input);
  EXPECT_FALSE(writer.begin(NAME, input.size()));
  EXPECT_EQ(fake::files.at(PATH)->bytes, input);
}
TEST_F(StageTest, RejectsInvalidNamesSizesAndAllocationBeforeCreating) {
  EXPECT_FALSE(writer.begin("../bad.epub", 1));
  EXPECT_FALSE(writer.takeResult());
  EXPECT_FALSE(writer.begin(NAME, StageWriter::MAX_BYTES + 1));
  EXPECT_FALSE(writer.takeResult());
  failAllocation = true;
  EXPECT_FALSE(writer.begin(NAME, 1));
  EXPECT_TRUE(fake::files.empty());
}
TEST_F(StageTest, SizeParserIsCanonicalAndBounded) {
  uint64_t result = 77;
  EXPECT_TRUE(syncstage::parseSize("0", result));
  EXPECT_EQ(result, 0u);
  EXPECT_TRUE(syncstage::parseSize("2147482623", result));
  EXPECT_EQ(result, StageWriter::MAX_BYTES);
  for (const auto* bad : {"", "00", "01", "-1", " 1", "1x", "2147482624", "99999999999999999999"}) {
    result = 77;
    EXPECT_FALSE(syncstage::parseSize(bad, result));
    EXPECT_EQ(result, 77u);
  }
}
TEST_F(StageTest, DigestParserRejectsMalformedInputWithoutChangingOutput) {
  uint8_t output[32];
  std::fill(std::begin(output), std::end(output), 0x5a);
  EXPECT_FALSE(syncstage::parseDigest(std::string(63, 'a'), output));
  EXPECT_FALSE(syncstage::parseDigest(std::string(63, 'a') + "z", output));
  EXPECT_TRUE(std::all_of(std::begin(output), std::end(output), [](uint8_t b) { return b == 0x5a; }));
  EXPECT_TRUE(syncstage::parseDigest(std::string(64, 'A'), output));
  EXPECT_TRUE(std::all_of(std::begin(output), std::end(output), [](uint8_t b) { return b == 0xaa; }));
}
TEST_F(StageTest, IncompleteOrOversizedInputCannotSucceed) {
  uint8_t byte = 7;
  ASSERT_TRUE(writer.begin(NAME, 2));
  ASSERT_TRUE(writer.write(&byte, 1));
  EXPECT_FALSE(writer.finish());
  EXPECT_FALSE(writer.takeResult());
  ASSERT_TRUE(fake::files.count(PATH));
  fake::files.clear();
  ASSERT_TRUE(writer.begin(NAME, 0));
  EXPECT_FALSE(writer.write(&byte, 1));
  EXPECT_FALSE(writer.takeResult());
}
TEST_F(StageTest, FaultsLeaveStageForReviewAndNeverReportSuccess) {
  std::vector<uint8_t> input(5000, 42);
  ASSERT_TRUE(writer.begin(NAME, input.size()));
  fake::failWrite = true;
  EXPECT_FALSE(writer.write(input.data(), input.size()));
  EXPECT_FALSE(writer.takeResult());
  EXPECT_EQ(fake::files.at(PATH)->bytes.size(), 2u);
  fake::files.clear();
  fake::failWrite = false;
  ASSERT_TRUE(writer.begin(NAME, 1));
  ASSERT_TRUE(writer.write(input.data(), 1));
  fake::failClose = true;
  EXPECT_FALSE(writer.finish());
  EXPECT_FALSE(writer.takeResult());
  EXPECT_EQ(fake::files.at(PATH)->bytes.size(), 1u);
}
}  // namespace
