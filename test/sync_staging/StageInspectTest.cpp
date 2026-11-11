#include <Memory.h>
#include <gtest/gtest.h>

#include <cstring>

#include "network/sync/StageInspect.h"

namespace {
using namespace syncstage;
constexpr char NAME[] = "__xsync_0123456789abcdef0123456789abcdef.part";
constexpr char PATH[] = "/__xsync_0123456789abcdef0123456789abcdef.part";
constexpr uint8_t SHA[] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                           0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                           0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
bool proceed(void*) { return true; }
bool cancel(void*) { return false; }
bool mutate(void*) {
  ++fake::epoch;
  return true;
}
class InspectStageTest : public ::testing::Test {
 protected:
  HalStorage storage;
  uint8_t hash[32]{};
  void SetUp() override {
    fake::files.clear();
    fake::directories = {"/", "/.crosspoint"};
    fake::epoch = 0;
    fake::writers = 0;
    fake::failWrite = fake::failClose = fake::failRead = fake::failSync = fake::missingParent = failAllocation = false;
  }
};
TEST_F(InspectStageTest, DistinguishesMissingWrongSizeAndComplete) {
  EXPECT_EQ(inspectStage(storage, NAME, 3, hash, proceed, nullptr), StageInspectResult::Missing);
  auto file = std::make_shared<fake::File>();
  file->bytes = {'a', 'b'};
  fake::files[PATH] = file;
  EXPECT_EQ(inspectStage(storage, NAME, 3, hash, proceed, nullptr), StageInspectResult::WrongSize);
  file->bytes = {'a', 'b', 'c'};
  EXPECT_EQ(inspectStage(storage, NAME, 3, hash, proceed, nullptr), StageInspectResult::Complete);
  EXPECT_EQ(memcmp(hash, SHA, sizeof(hash)), 0);
}
TEST_F(InspectStageTest, ErrorsAndChangesNeverClaimMissingOrComplete) {
  auto file = std::make_shared<fake::File>();
  file->bytes = {'a', 'b', 'c'};
  fake::files[PATH] = file;
  fake::failRead = true;
  EXPECT_EQ(inspectStage(storage, NAME, 3, hash, proceed, nullptr), StageInspectResult::Unavailable);
  fake::failRead = false;
  EXPECT_EQ(inspectStage(storage, NAME, 3, hash, cancel, nullptr), StageInspectResult::Cancelled);
  EXPECT_EQ(inspectStage(storage, NAME, 3, hash, mutate, nullptr), StageInspectResult::Changed);
  EXPECT_EQ(inspectStage(storage, "bad", 3, hash, proceed, nullptr), StageInspectResult::Invalid);
}
}  // namespace
