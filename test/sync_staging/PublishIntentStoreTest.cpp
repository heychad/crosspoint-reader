#include <Memory.h>
#include <gtest/gtest.h>

#include <algorithm>

#include "network/sync/PublishIntentStore.h"

namespace {
constexpr char STAGE[] = "__xsync_0123456789abcdef0123456789abcdef.part";
class IntentStoreTest : public ::testing::Test {
 protected:
  HalStorage storage;
  syncpublish::IntentView intent{STAGE, "/Authors/Book.epub", 3, {}};
  void SetUp() override {
    fake::files.clear();
    fake::epoch = 0;
    fake::failWrite = fake::failClose = fake::failRead = fake::failSync = fake::missingParent = failAllocation = false;
  }
};
TEST_F(IntentStoreTest, PrepareInspectAndExistingPreserveBytes) {
  using namespace syncpublish;
  Record output{};
  size_t size = 99;
  EXPECT_EQ(inspectIntent(storage, output, size), InspectResult::Missing);
  EXPECT_EQ(size, 99u);
  ASSERT_EQ(prepareIntent(storage, intent), PrepareResult::Prepared);
  ASSERT_EQ(inspectIntent(storage, output, size), InspectResult::Valid);
  IntentView decoded{};
  ASSERT_TRUE(decode(output.data(), size, decoded));
  EXPECT_EQ(decoded.destination, intent.destination);
  const auto before = fake::files.at(INTENT_PATH)->bytes;
  EXPECT_EQ(prepareIntent(storage, intent), PrepareResult::Existing);
  EXPECT_EQ(fake::files.at(INTENT_PATH)->bytes, before);
}
TEST_F(IntentStoreTest, DamagedRecordAndReadErrorLeaveOutputUntouched) {
  using namespace syncpublish;
  ASSERT_EQ(prepareIntent(storage, intent), PrepareResult::Prepared);
  fake::files.at(INTENT_PATH)->bytes[0] ^= 1;
  Record output;
  output.fill(0x5a);
  size_t size = 77;
  EXPECT_EQ(inspectIntent(storage, output, size), InspectResult::Damaged);
  EXPECT_EQ(size, 77u);
  EXPECT_TRUE(std::all_of(output.begin(), output.end(), [](uint8_t b) { return b == 0x5a; }));
  fake::failRead = true;
  EXPECT_EQ(inspectIntent(storage, output, size), InspectResult::Unavailable);
  EXPECT_EQ(size, 77u);
}
TEST_F(IntentStoreTest, FailuresKeepAnyIntentForReview) {
  using namespace syncpublish;
  fake::missingParent = true;
  EXPECT_EQ(prepareIntent(storage, intent), PrepareResult::Unavailable);
  EXPECT_TRUE(fake::files.empty());
  fake::missingParent = false;
  fake::failSync = true;
  EXPECT_EQ(prepareIntent(storage, intent), PrepareResult::Uncertain);
  EXPECT_TRUE(fake::files.count(INTENT_PATH));
  fake::failSync = false;
  EXPECT_EQ(prepareIntent(storage, intent), PrepareResult::Existing);
}
}  // namespace
