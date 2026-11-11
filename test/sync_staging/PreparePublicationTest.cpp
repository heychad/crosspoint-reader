#include <Memory.h>
#include <gtest/gtest.h>

#include "network/sync/PreparePublication.h"

namespace {
constexpr char STAGE[] = "__xsync_0123456789abcdef0123456789abcdef.part";
constexpr char STAGE_PATH[] = "/__xsync_0123456789abcdef0123456789abcdef.part";
constexpr char DEST[] = "/Authors/Book.epub";
constexpr syncpublish::Digest ABC_SHA = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                                         0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                                         0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
bool proceed(void*) { return true; }
struct Race {
  int calls = 0;
};
bool mutateAtCommit(void* raw) {
  auto& r = *static_cast<Race*>(raw);
  if (++r.calls == 3) ++fake::epoch;
  return true;
}
class PrepareTest : public ::testing::Test {
 protected:
  HalStorage storage;
  syncpublish::IntentView intent{STAGE, DEST, 3, ABC_SHA};
  void SetUp() override {
    fake::files.clear();
    fake::directories = {"/", "/.crosspoint", "/Authors"};
    fake::epoch = 0;
    fake::failWrite = fake::failClose = fake::failRead = fake::failSync = fake::missingParent = failAllocation = false;
    auto file = std::make_shared<fake::File>();
    file->bytes = {'a', 'b', 'c'};
    fake::files[STAGE_PATH] = file;
  }
};
TEST_F(PrepareTest, VerifiedStageCreatesIntentWithoutMovingBook) {
  using namespace syncpublish;
  ASSERT_EQ(prepareVerifiedIntent(storage, intent, proceed, nullptr), PreflightResult::Prepared);
  EXPECT_TRUE(fake::files.count(STAGE_PATH));
  EXPECT_FALSE(fake::files.count(DEST));
  Record record{};
  size_t length = 0;
  ASSERT_EQ(inspectIntent(storage, record, length), InspectResult::Valid);
  IntentView decoded{};
  ASSERT_TRUE(decode(record.data(), length, decoded));
  EXPECT_EQ(decoded.destination, DEST);
  EXPECT_EQ(prepareVerifiedIntent(storage, intent, proceed, nullptr), PreflightResult::ExistingIntent);
}
TEST_F(PrepareTest, RejectsMissingStageWrongHashAndExistingDestination) {
  using namespace syncpublish;
  fake::files.erase(STAGE_PATH);
  EXPECT_EQ(prepareVerifiedIntent(storage, intent, proceed, nullptr), PreflightResult::StageMissing);
  auto file = std::make_shared<fake::File>();
  file->bytes = {'x', 'b', 'c'};
  fake::files[STAGE_PATH] = file;
  EXPECT_EQ(prepareVerifiedIntent(storage, intent, proceed, nullptr), PreflightResult::HashMismatch);
  file->bytes = {'a', 'b', 'c'};
  fake::files[DEST] = std::make_shared<fake::File>();
  EXPECT_EQ(prepareVerifiedIntent(storage, intent, proceed, nullptr), PreflightResult::ReservedState);
  fake::files.erase(DEST);
  fake::files[std::string(DEST) + ".meta.json"] = std::make_shared<fake::File>();
  EXPECT_EQ(prepareVerifiedIntent(storage, intent, proceed, nullptr), PreflightResult::ReservedState);
  EXPECT_FALSE(fake::files.count(INTENT_PATH));
}
TEST_F(PrepareTest, RejectsMissingParentAndTrackedChangeBeforeCommit) {
  using namespace syncpublish;
  fake::directories.erase("/Authors");
  EXPECT_EQ(prepareVerifiedIntent(storage, intent, proceed, nullptr), PreflightResult::ParentMissing);
  fake::directories.insert("/Authors");
  Race race;
  EXPECT_EQ(prepareVerifiedIntent(storage, intent, mutateAtCommit, &race), PreflightResult::Changed);
  EXPECT_FALSE(fake::files.count(INTENT_PATH));
}
TEST_F(PrepareTest, RejectsReferencedBookBeforeCreatingIntent) {
  using namespace syncpublish;
  auto file = std::make_shared<fake::File>();
  const std::string json = R"({"openEpubPath":"/elsewhere/Book.epub"})";
  file->bytes.assign(json.begin(), json.end());
  fake::files["/.crosspoint/state.json"] = file;
  EXPECT_EQ(prepareVerifiedIntent(storage, intent, proceed, nullptr), PreflightResult::ReservedState);
  EXPECT_FALSE(fake::files.count(INTENT_PATH));
}
}  // namespace
