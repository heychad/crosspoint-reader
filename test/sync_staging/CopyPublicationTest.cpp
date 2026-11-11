#include <Memory.h>
#include <gtest/gtest.h>

#include "network/sync/CopyPublication.h"

namespace {
constexpr char STAGE[] = "__xsync_0123456789abcdef0123456789abcdef.part";
constexpr char SOURCE[] = "/__xsync_0123456789abcdef0123456789abcdef.part";
constexpr char PRIVATE[] = "/.crosspoint/__xsync_0123456789abcdef0123456789abcdef.part";
constexpr syncpublish::Digest ABC_SHA = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                         0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                         0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
bool proceed(void*) { return true; }
struct Race {
  int calls = 0;
  bool cancel = false;
};
bool interrupt(void* raw) {
  auto& r = *static_cast<Race*>(raw);
  if (++r.calls == 4) {
    if (r.cancel) return false;
    ++fake::epoch;
  }
  return true;
}
class CopyTest : public ::testing::Test {
 protected:
  HalStorage storage;
  syncpublish::IntentView intent{STAGE, "/Authors/Book.epub", 3, ABC_SHA};
  void SetUp() override {
    fake::files.clear();
    fake::directories = {"/", "/.crosspoint", "/Authors"};
    fake::epoch = 0;
    fake::writers = 0;
    fake::failWrite = fake::failClose = fake::failRead = fake::failSync = fake::missingParent = failAllocation = false;
    auto file = std::make_shared<fake::File>();
    file->bytes = {'a', 'b', 'c'};
    fake::files[SOURCE] = file;
    ASSERT_EQ(syncpublish::prepareIntent(storage, intent), syncpublish::PrepareResult::Prepared);
  }
};
TEST_F(CopyTest, CopiesAndVerifiesWithoutMovingSourceOrIntent) {
  using namespace syncpublish;
  ASSERT_EQ(copyVerifiedStage(storage, intent, proceed, nullptr), CopyResult::Copied);
  EXPECT_EQ(fake::files.at(PRIVATE)->bytes, fake::files.at(SOURCE)->bytes);
  EXPECT_TRUE(fake::files.count(INTENT_PATH));
  EXPECT_FALSE(fake::files.count("/Authors/Book.epub"));
  EXPECT_EQ(fake::writers, 0u);
  EXPECT_EQ(copyVerifiedStage(storage, intent, proceed, nullptr), CopyResult::VerifiedExistingCopy);
}
TEST_F(CopyTest, ChangedOrCancelledCopyIsRetainedForReview) {
  using namespace syncpublish;
  Race race;
  EXPECT_EQ(copyVerifiedStage(storage, intent, interrupt, &race), CopyResult::ChangedPartial);
  EXPECT_TRUE(fake::files.count(PRIVATE));
  EXPECT_TRUE(fake::files.count(SOURCE));
  EXPECT_EQ(fake::writers, 0u);
  fake::files.erase(PRIVATE);
  Race cancel{0, true};
  EXPECT_EQ(copyVerifiedStage(storage, intent, interrupt, &cancel), CopyResult::CancelledPartial);
  EXPECT_TRUE(fake::files.count(PRIVATE));
  EXPECT_EQ(fake::writers, 0u);
}
TEST_F(CopyTest, RejectsChangedSourceOrDifferentIntent) {
  using namespace syncpublish;
  fake::files.at(SOURCE)->bytes[0] = 'x';
  EXPECT_EQ(copyVerifiedStage(storage, intent, proceed, nullptr), CopyResult::Conflict);
  EXPECT_FALSE(fake::files.count(PRIVATE));
  fake::files.at(SOURCE)->bytes[0] = 'a';
  auto other = intent;
  other.destination = "/Authors/Else.epub";
  EXPECT_EQ(copyVerifiedStage(storage, other, proceed, nullptr), CopyResult::Conflict);
  EXPECT_FALSE(fake::files.count(PRIVATE));
}
TEST_F(CopyTest, ExistingIncompleteOrDifferentBytesAreNeverReplaced) {
  using namespace syncpublish;
  auto file = std::make_shared<fake::File>();
  file->bytes = {'x'};
  fake::files[PRIVATE] = file;
  EXPECT_EQ(copyVerifiedStage(storage, intent, proceed, nullptr), CopyResult::Copied);
  EXPECT_EQ(file->bytes, std::vector<uint8_t>({'x'}));
  EXPECT_EQ(fake::files.at(std::string(PRIVATE) + "-r1")->bytes, std::vector<uint8_t>({'a', 'b', 'c'}));
  EXPECT_EQ(copyVerifiedStage(storage, intent, proceed, nullptr), CopyResult::VerifiedExistingCopy);
  for (unsigned i = 1; i < syncstage::PRIVATE_COPY_SLOTS; ++i)
    fake::files[std::string(PRIVATE) + "-r" + std::to_string(i)] = std::make_shared<fake::File>();
  const auto epoch = fake::epoch;
  EXPECT_EQ(copyVerifiedStage(storage, intent, proceed, nullptr), CopyResult::ExistingCopy);
  EXPECT_EQ(file->bytes, std::vector<uint8_t>({'x'}));
  EXPECT_EQ(fake::epoch, epoch);
}
TEST_F(CopyTest, ExistingCopyVerificationCanBeCancelledOrChanged) {
  using namespace syncpublish;
  auto file = std::make_shared<fake::File>();
  file->bytes = {'a', 'b', 'c'};
  fake::files[PRIVATE] = file;
  Race cancelled{0, true};
  EXPECT_EQ(copyVerifiedStage(storage, intent, interrupt, &cancelled), CopyResult::Cancelled);
  Race changed;
  EXPECT_EQ(copyVerifiedStage(storage, intent, interrupt, &changed), CopyResult::Changed);
  EXPECT_EQ(file->bytes, std::vector<uint8_t>({'a', 'b', 'c'}));
}
}  // namespace
