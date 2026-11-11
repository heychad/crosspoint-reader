#include <Memory.h>
#include <gtest/gtest.h>

#include <cstring>

#include "network/sync/RecoveryPreview.h"

namespace {
constexpr char STAGE[] = "__xsync_0123456789abcdef0123456789abcdef.part";
constexpr char STAGE_PATH[] = "/__xsync_0123456789abcdef0123456789abcdef.part";
constexpr char COPY_PATH[] = "/.crosspoint/__xsync_0123456789abcdef0123456789abcdef.part";
constexpr char DEST[] = "/Authors/Book.epub";
constexpr syncpublish::Digest ABC_SHA = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                         0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                         0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
bool proceed(void*) { return true; }
bool cancel(void*) { return false; }
bool change(void*) {
  ++fake::epoch;
  return true;
}
class PreviewTest : public ::testing::Test {
 protected:
  HalStorage storage;
  syncpublish::RecoveryPreview output{};
  void SetUp() override {
    fake::files.clear();
    fake::epoch = 0;
    fake::failWrite = fake::failClose = fake::failRead = fake::failSync = fake::missingParent = failAllocation = false;
    output.size = 77;
  }
  void prepare() {
    syncpublish::IntentView intent{STAGE, DEST, 3, ABC_SHA};
    ASSERT_EQ(syncpublish::prepareIntent(storage, intent), syncpublish::PrepareResult::Prepared);
  }
  void stage(std::initializer_list<uint8_t> bytes) {
    auto f = std::make_shared<fake::File>();
    f->bytes = bytes;
    fake::files[STAGE_PATH] = f;
    ++fake::epoch;
  }
};
TEST_F(PreviewTest, NoIntentAndDamagedIntentDoNotChangeOutput) {
  using namespace syncpublish;
  EXPECT_EQ(previewRecovery(storage, output, proceed, nullptr), PreviewResult::NoIntent);
  EXPECT_EQ(output.size, 77u);
  prepare();
  fake::files.at(INTENT_PATH)->bytes[0] ^= 1;
  EXPECT_EQ(previewRecovery(storage, output, proceed, nullptr), PreviewResult::Damaged);
  EXPECT_EQ(output.size, 77u);
}
TEST_F(PreviewTest, ReportsMissingMatchingAndModifiedStages) {
  using namespace syncpublish;
  prepare();
  ASSERT_EQ(previewRecovery(storage, output, proceed, nullptr), PreviewResult::Observed);
  EXPECT_EQ(output.stageState, StageState::Missing);
  EXPECT_EQ(output.destinationState, DestinationState::Missing);
  EXPECT_EQ(output.copyState, StageState::Missing);
  stage({'a', 'b', 'c'});
  ASSERT_EQ(previewRecovery(storage, output, proceed, nullptr), PreviewResult::Observed);
  EXPECT_EQ(output.stageState, StageState::Matches);
  EXPECT_STREQ(output.destination.data(), DEST);
  stage({'x', 'b', 'c'});
  ASSERT_EQ(previewRecovery(storage, output, proceed, nullptr), PreviewResult::Observed);
  EXPECT_EQ(output.stageState, StageState::Mismatch);
  stage({'a'});
  ASSERT_EQ(previewRecovery(storage, output, proceed, nullptr), PreviewResult::Observed);
  EXPECT_EQ(output.stageState, StageState::Mismatch);
}
TEST_F(PreviewTest, ReportsPrivateCopyWithoutTreatingItAsPublication) {
  using namespace syncpublish;
  prepare();
  stage({'a', 'b', 'c'});
  auto copy = std::make_shared<fake::File>();
  copy->bytes = {'a', 'b', 'c'};
  fake::files[COPY_PATH] = copy;
  ASSERT_EQ(previewRecovery(storage, output, proceed, nullptr), PreviewResult::Observed);
  EXPECT_EQ(output.copyState, StageState::Matches);
  EXPECT_EQ(output.destinationState, DestinationState::Missing);
  EXPECT_EQ(output.copySlot, 0u);
  copy->bytes = {'x', 'b', 'c'};
  ++fake::epoch;
  ASSERT_EQ(previewRecovery(storage, output, proceed, nullptr), PreviewResult::Observed);
  EXPECT_EQ(output.copyState, StageState::Mismatch);
  auto retry = std::make_shared<fake::File>();
  retry->bytes = {'a', 'b', 'c'};
  fake::files[std::string(COPY_PATH) + "-r1"] = retry;
  ++fake::epoch;
  ASSERT_EQ(previewRecovery(storage, output, proceed, nullptr), PreviewResult::Observed);
  EXPECT_EQ(output.copyState, StageState::Matches);
  EXPECT_EQ(output.copySlot, 1u);
}
TEST_F(PreviewTest, ReportsExistingDestinationWithoutChangingFiles) {
  using namespace syncpublish;
  prepare();
  stage({'a', 'b', 'c'});
  fake::files[DEST] = std::make_shared<fake::File>();
  const auto count = fake::files.size();
  ASSERT_EQ(previewRecovery(storage, output, proceed, nullptr), PreviewResult::Observed);
  EXPECT_EQ(output.stageState, StageState::Matches);
  EXPECT_EQ(output.destinationState, DestinationState::Mismatch);
  fake::files[DEST]->bytes = {'a', 'b', 'c'};
  ++fake::epoch;
  ASSERT_EQ(previewRecovery(storage, output, proceed, nullptr), PreviewResult::Observed);
  EXPECT_EQ(output.destinationState, DestinationState::Matches);
  EXPECT_EQ(fake::files.size(), count);
}
TEST_F(PreviewTest, CancellationOrMutationNeverReturnAnObservation) {
  using namespace syncpublish;
  prepare();
  stage({'a', 'b', 'c'});
  EXPECT_EQ(previewRecovery(storage, output, cancel, nullptr), PreviewResult::Cancelled);
  EXPECT_EQ(output.size, 77u);
  EXPECT_EQ(previewRecovery(storage, output, change, nullptr), PreviewResult::Changed);
  EXPECT_EQ(output.size, 77u);
}
}  // namespace
