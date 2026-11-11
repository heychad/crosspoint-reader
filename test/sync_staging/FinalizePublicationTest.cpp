#include <Memory.h>
#include <gtest/gtest.h>

#include "network/sync/ArchivePublication.h"
#include "network/sync/CleanupPublication.h"
#include "network/sync/FinalizePublication.h"
#include "network/sync/MemoryReferenceGuard.h"
#include "network/sync/PublishIntentStore.h"

namespace {
using namespace syncpublish;
constexpr char STAGE[] = "__xsync_0123456789abcdef0123456789abcdef.part";
constexpr char SOURCE[] = "/__xsync_0123456789abcdef0123456789abcdef.part";
constexpr char COPY[] = "/.crosspoint/__xsync_0123456789abcdef0123456789abcdef.part-r1";
constexpr char DEST[] = "/Authors/Book.epub";
constexpr char ARCHIVE[] = "/.crosspoint/xsync-0123456789abcdef0123456789abcdef.done";
constexpr Digest SHA = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
                        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
bool yes(void*) { return true; }
bool no(void*) { return false; }
void release(void*) {}
struct LockState {
  bool held = false;
  int acquisitions = 0;
  int releases = 0;
};
bool acquireTracked(void* raw) {
  auto& state = *static_cast<LockState*>(raw);
  state.held = true;
  ++state.acquisitions;
  return true;
}
void releaseTracked(void* raw) {
  auto& state = *static_cast<LockState*>(raw);
  state.held = false;
  ++state.releases;
}
bool allow(const char*, void*) { return true; }
bool deny(const char*, void*) { return false; }
bool mutate(const char*, void*) {
  ++fake::epoch;
  return true;
}
struct Recent {
  std::string path;
};
struct MemoryState {
  std::string open;
  std::vector<Recent> recent;
};
bool checkMemory(const char* destination, void* context) {
  auto& state = *static_cast<MemoryState*>(context);
  return memoryReferencesClear(state.open, state.recent, destination);
}
class FinalizeTest : public ::testing::Test {
 protected:
  HalStorage storage;
  void SetUp() override {
    fake::files.clear();
    fake::directories = {"/", "/.crosspoint", "/Authors"};
    fake::epoch = 0;
    fake::writers = 0;
    fake::failWrite = fake::failClose = fake::failRead = fake::failSync = fake::missingParent = fake::failRename =
        fake::failRemove = failAllocation = false;
    auto stage = std::make_shared<fake::File>();
    stage->bytes = {'a', 'b', 'c'};
    fake::files[SOURCE] = stage;
    auto copy = std::make_shared<fake::File>();
    copy->bytes = stage->bytes;
    fake::files[COPY] = copy;
    ASSERT_EQ(prepareIntent(storage, {STAGE, DEST, 3, SHA}), PrepareResult::Prepared);
  }
  FinalizeResult run(PublicationGate gate = allow) {
    return finalizeVerifiedCopy(storage, yes, nullptr, gate, nullptr, yes, release, nullptr);
  }
};
TEST_F(FinalizeTest, PublishesVerifiedRetryAndRetainsSourceAndIntent) {
  ASSERT_EQ(run(), FinalizeResult::Published);
  EXPECT_FALSE(fake::files.count(COPY));
  EXPECT_TRUE(fake::files.count(SOURCE));
  EXPECT_TRUE(fake::files.count(INTENT_PATH));
  EXPECT_EQ(fake::files.at(DEST)->bytes, std::vector<uint8_t>({'a', 'b', 'c'}));
  EXPECT_EQ(run(), FinalizeResult::MatchingDestination);
  EXPECT_TRUE(fake::files.count(INTENT_PATH));
  EXPECT_TRUE(fake::files.count(SOURCE));
}
TEST_F(FinalizeTest, RefusesExistingOrReferencedDestination) {
  auto existing = std::make_shared<fake::File>();
  existing->bytes = {'z'};
  fake::files[DEST] = existing;
  EXPECT_EQ(run(), FinalizeResult::Conflict);
  EXPECT_EQ(existing->bytes, std::vector<uint8_t>({'z'}));
  fake::files.erase(DEST);
  const std::string state = R"({"openEpubPath":"/other/Book.epub"})";
  auto f = std::make_shared<fake::File>();
  f->bytes.assign(state.begin(), state.end());
  fake::files["/.crosspoint/state.json"] = f;
  EXPECT_EQ(run(), FinalizeResult::Conflict);
  EXPECT_TRUE(fake::files.count(COPY));
}
TEST_F(FinalizeTest, RequiresGateAndTreatsRenameFailureAsUncertain) {
  EXPECT_EQ(run(deny), FinalizeResult::Conflict);
  EXPECT_EQ(run(mutate), FinalizeResult::Changed);
  fake::failRename = true;
  EXPECT_EQ(run(), FinalizeResult::Uncertain);
  EXPECT_TRUE(fake::files.count(COPY));
  EXPECT_FALSE(fake::files.count(DEST));
}
TEST_F(FinalizeTest, ReleasesPublicationLockWhenGateRejects) {
  LockState state;
  EXPECT_EQ(finalizeVerifiedCopy(storage, yes, nullptr, deny, nullptr, acquireTracked, releaseTracked, &state),
            FinalizeResult::Conflict);
  EXPECT_FALSE(state.held);
  EXPECT_EQ(state.acquisitions, 1);
  EXPECT_EQ(state.releases, 1);
  EXPECT_TRUE(fake::files.count(COPY));
  EXPECT_FALSE(fake::files.count(DEST));
}
TEST_F(FinalizeTest, RejectsWrongCopyAndCancellation) {
  fake::files[COPY]->bytes = {'x', 'b', 'c'};
  EXPECT_EQ(run(), FinalizeResult::Conflict);
  EXPECT_EQ(finalizeVerifiedCopy(storage, no, nullptr, allow, nullptr, yes, release, nullptr),
            FinalizeResult::Cancelled);
  EXPECT_FALSE(fake::files.count(DEST));
}
TEST_F(FinalizeTest, GateChecksInMemoryOpenAndRecentReferences) {
  MemoryState state{"/other/Book.epub", {}};
  EXPECT_EQ(finalizeVerifiedCopy(storage, yes, nullptr, checkMemory, &state, yes, release, nullptr),
            FinalizeResult::Conflict);
  state.open = "/other/different.epub";
  state.recent.push_back({"/other/BOOK.epub"});
  EXPECT_EQ(finalizeVerifiedCopy(storage, yes, nullptr, checkMemory, &state, yes, release, nullptr),
            FinalizeResult::Conflict);
  state.recent[0].path = "/other/different.epub";
  EXPECT_EQ(finalizeVerifiedCopy(storage, yes, nullptr, checkMemory, &state, yes, release, nullptr),
            FinalizeResult::Published);
}
TEST_F(FinalizeTest, MatchingPreexistingDestinationIsOnlyObserved) {
  auto existing = std::make_shared<fake::File>();
  existing->bytes = {'a', 'b', 'c'};
  fake::files[DEST] = existing;
  const auto before = fake::epoch;
  EXPECT_EQ(run(deny), FinalizeResult::MatchingDestination);
  EXPECT_EQ(fake::epoch, before);
  EXPECT_TRUE(fake::files.count(COPY));
  EXPECT_TRUE(fake::files.count(INTENT_PATH));
}
TEST_F(FinalizeTest, ArchivesCompletedIntentWithoutDeletingAnyBook) {
  EXPECT_EQ(archiveVerifiedIntent(storage, STAGE, yes, nullptr), ArchiveResult::Conflict);
  ASSERT_EQ(run(), FinalizeResult::Published);
  EXPECT_EQ(archiveVerifiedIntent(storage, STAGE, yes, nullptr), ArchiveResult::Archived);
  EXPECT_TRUE(fake::files.count(ARCHIVE));
  EXPECT_FALSE(fake::files.count(INTENT_PATH));
  EXPECT_TRUE(fake::files.count(SOURCE));
  EXPECT_TRUE(fake::files.count(DEST));
  const auto epoch = fake::epoch;
  EXPECT_EQ(archiveVerifiedIntent(storage, STAGE, yes, nullptr), ArchiveResult::AlreadyArchived);
  EXPECT_EQ(fake::epoch, epoch);
  fake::files.at(DEST)->bytes[0] = 'x';
  EXPECT_EQ(archiveVerifiedIntent(storage, STAGE, yes, nullptr), ArchiveResult::Conflict);
}
TEST_F(FinalizeTest, ArchiveRefusesExistingReceiptAndRenameFailure) {
  ASSERT_EQ(run(), FinalizeResult::Published);
  fake::files[ARCHIVE] = std::make_shared<fake::File>();
  EXPECT_EQ(archiveVerifiedIntent(storage, STAGE, yes, nullptr), ArchiveResult::Conflict);
  EXPECT_TRUE(fake::files.count(INTENT_PATH));
  fake::files.erase(ARCHIVE);
  fake::failRename = true;
  EXPECT_EQ(archiveVerifiedIntent(storage, STAGE, yes, nullptr), ArchiveResult::Uncertain);
  EXPECT_TRUE(fake::files.count(INTENT_PATH));
  EXPECT_TRUE(fake::files.count(SOURCE));
  EXPECT_TRUE(fake::files.count(DEST));
}
TEST_F(FinalizeTest, ArchiveDoesNotTrustDamagedReceiptOrWrongStage) {
  ASSERT_EQ(run(), FinalizeResult::Published);
  EXPECT_EQ(archiveVerifiedIntent(storage, STAGE, no, nullptr), ArchiveResult::Cancelled);
  EXPECT_EQ(archiveVerifiedIntent(storage, "invalid", yes, nullptr), ArchiveResult::Unavailable);
  ASSERT_EQ(archiveVerifiedIntent(storage, STAGE, yes, nullptr), ArchiveResult::Archived);
  fake::files.at(ARCHIVE)->bytes[0] ^= 1;
  EXPECT_EQ(archiveVerifiedIntent(storage, STAGE, yes, nullptr), ArchiveResult::Conflict);
  EXPECT_TRUE(fake::files.count(SOURCE));
  EXPECT_TRUE(fake::files.count(DEST));
}
TEST_F(FinalizeTest, CleansOnlyVerifiedArchivedRootStage) {
  ASSERT_EQ(run(), FinalizeResult::Published);
  ASSERT_EQ(archiveVerifiedIntent(storage, STAGE, yes, nullptr), ArchiveResult::Archived);
  EXPECT_EQ(cleanupArchivedStage(storage, STAGE, yes, nullptr), CleanupResult::Cleaned);
  EXPECT_FALSE(fake::files.count(SOURCE));
  EXPECT_TRUE(fake::files.count(DEST));
  EXPECT_TRUE(fake::files.count(ARCHIVE));
  EXPECT_EQ(cleanupArchivedStage(storage, STAGE, yes, nullptr), CleanupResult::AlreadyClean);
  EXPECT_EQ(archiveVerifiedIntent(storage, STAGE, yes, nullptr), ArchiveResult::AlreadyArchived);
}
TEST_F(FinalizeTest, CleanupRefusesChangedDestinationOrSourceAndRetainsFilesOnFailure) {
  ASSERT_EQ(run(), FinalizeResult::Published);
  ASSERT_EQ(archiveVerifiedIntent(storage, STAGE, yes, nullptr), ArchiveResult::Archived);
  fake::files[DEST]->bytes[0] = 'x';
  EXPECT_EQ(cleanupArchivedStage(storage, STAGE, yes, nullptr), CleanupResult::Conflict);
  fake::files[DEST]->bytes[0] = 'a';
  fake::files[SOURCE]->bytes[0] = 'x';
  EXPECT_EQ(cleanupArchivedStage(storage, STAGE, yes, nullptr), CleanupResult::Conflict);
  fake::files[SOURCE]->bytes[0] = 'a';
  fake::failRemove = true;
  EXPECT_EQ(cleanupArchivedStage(storage, STAGE, yes, nullptr), CleanupResult::Uncertain);
  EXPECT_TRUE(fake::files.count(SOURCE));
  EXPECT_TRUE(fake::files.count(DEST));
  EXPECT_TRUE(fake::files.count(ARCHIVE));
}
TEST_F(FinalizeTest, ArchivedBookRemainsRecognizableWithDamagedStage) {
  ASSERT_EQ(run(), FinalizeResult::Published);
  ASSERT_EQ(archiveVerifiedIntent(storage, STAGE, yes, nullptr), ArchiveResult::Archived);
  fake::files[SOURCE]->bytes = {'x'};
  EXPECT_EQ(archiveVerifiedIntent(storage, STAGE, yes, nullptr), ArchiveResult::AlreadyArchived);
  EXPECT_EQ(cleanupArchivedStage(storage, STAGE, yes, nullptr), CleanupResult::Conflict);
  EXPECT_TRUE(fake::files.count(SOURCE));
  EXPECT_TRUE(fake::files.count(DEST));
}
}  // namespace
