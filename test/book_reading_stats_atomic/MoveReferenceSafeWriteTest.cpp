#include <HalStorage.h>
#include <MoveReferenceSafeWrite.h>
#include <gtest/gtest.h>

#include <string>

namespace {
constexpr char PRIMARY[] = "/.crosspoint/state.json";
constexpr char TEMP[] = "/.crosspoint/state.json.tmp";
constexpr char BACKUP[] = "/.crosspoint/state.json.bak";
class MoveReferenceSafeWrite : public testing::Test {
 protected:
  void SetUp() override {
    Storage.reset();
    HalFile file;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", PRIMARY, file));
    constexpr char data[] = "{\"openEpubPath\":\"/old.epub\"}";
    ASSERT_EQ(file.write(data, sizeof(data) - 1), sizeof(data) - 1);
    ASSERT_TRUE(file.close());
  }
  std::string content(const char* path) {
    HalFile file;
    if (!Storage.openFileForRead("TEST", path, file)) return {};
    std::string bytes(file.fileSize(), '\0');
    if (file.read(bytes.data(), bytes.size()) != static_cast<int>(bytes.size()) || !file.close()) return {};
    return bytes;
  }
};
constexpr char NEXT[] = "{\"openEpubPath\":\"/read/book.epub\"}";
}  // namespace

TEST_F(MoveReferenceSafeWrite, ArchivesOldStateWithoutDeletingIt) {
  ASSERT_TRUE(movereference::write(PRIMARY, NEXT, sizeof(NEXT) - 1));
  EXPECT_EQ(content(PRIMARY), NEXT);
  EXPECT_EQ(content("/.crosspoint/state.json.move-history-0000"), "{\"openEpubPath\":\"/old.epub\"}");
  EXPECT_FALSE(Storage.exists(TEMP));
  EXPECT_FALSE(Storage.exists(BACKUP));
  EXPECT_TRUE(movereference::write(PRIMARY, NEXT, sizeof(NEXT) - 1));
  EXPECT_FALSE(Storage.exists("/.crosspoint/state.json.move-history-0001"));
}

TEST_F(MoveReferenceSafeWrite, RetainsInterruptedStageAndOriginal) {
  Storage.failNextWriteFor(TEMP, 3);
  EXPECT_FALSE(movereference::write(PRIMARY, NEXT, sizeof(NEXT) - 1));
  EXPECT_TRUE(Storage.exists(TEMP));
  EXPECT_EQ(content(PRIMARY), "{\"openEpubPath\":\"/old.epub\"}");
  EXPECT_FALSE(movereference::write(PRIMARY, NEXT, sizeof(NEXT) - 1));
}

TEST_F(MoveReferenceSafeWrite, FailedPromotionPreservesBackupAndStage) {
  Storage.failNextRenameFrom(TEMP);
  EXPECT_FALSE(movereference::write(PRIMARY, NEXT, sizeof(NEXT) - 1));
  EXPECT_TRUE(Storage.exists(TEMP));
  EXPECT_EQ(content(BACKUP), "{\"openEpubPath\":\"/old.epub\"}");
  EXPECT_FALSE(Storage.exists(PRIMARY));
}

TEST_F(MoveReferenceSafeWrite, PublishedPrimaryFinishesBackupArchiveOnRetry) {
  const auto original = content(PRIMARY);
  Storage.failNextRenameFrom(BACKUP);
  EXPECT_FALSE(movereference::write(PRIMARY, NEXT, sizeof(NEXT) - 1));
  EXPECT_EQ(content(PRIMARY), NEXT);
  EXPECT_EQ(content(BACKUP), original);
  ASSERT_TRUE(movereference::write(PRIMARY, NEXT, sizeof(NEXT) - 1, true));
  EXPECT_EQ(content(PRIMARY), NEXT);
  EXPECT_EQ(content("/.crosspoint/state.json.move-history-0000"), original);
  EXPECT_FALSE(Storage.exists(BACKUP));
  EXPECT_FALSE(Storage.exists(TEMP));
}

TEST_F(MoveReferenceSafeWrite, FinishOnlyNeverPublishesOrReplacesAConflictingPrimary) {
  const auto original = content(PRIMARY);
  EXPECT_FALSE(movereference::write(PRIMARY, NEXT, sizeof(NEXT) - 1, true));
  EXPECT_EQ(content(PRIMARY), original);
  EXPECT_FALSE(Storage.exists(TEMP));
  EXPECT_FALSE(Storage.exists(BACKUP));
  Storage.failNextRenameFrom(TEMP);
  EXPECT_FALSE(movereference::write(PRIMARY, NEXT, sizeof(NEXT) - 1));
  EXPECT_FALSE(movereference::write(PRIMARY, NEXT, sizeof(NEXT) - 1, true));
  EXPECT_FALSE(Storage.exists(PRIMARY));
  EXPECT_EQ(content(BACKUP), original);
  EXPECT_EQ(content(TEMP), NEXT);
}

TEST_F(MoveReferenceSafeWrite, PublishedRecoveryPreservesArtifactsIfIdentityCannotBeProved) {
  Storage.failNextRenameFrom(BACKUP);
  EXPECT_FALSE(movereference::write(PRIMARY, NEXT, sizeof(NEXT) - 1));
  const auto original = content(BACKUP);
  Storage.failSettingsIdentity();
  EXPECT_FALSE(movereference::write(PRIMARY, NEXT, sizeof(NEXT) - 1, true));
  EXPECT_EQ(content(PRIMARY), NEXT);
  EXPECT_EQ(content(BACKUP), original);
}

TEST_F(MoveReferenceSafeWrite, PreservesExistingHistoryAndUsesNextSlot) {
  constexpr char history[] = "/.crosspoint/state.json.move-history-0000";
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", history, file));
  constexpr char prior[] = "prior";
  ASSERT_EQ(file.write(prior, sizeof(prior) - 1), sizeof(prior) - 1);
  ASSERT_TRUE(file.close());
  ASSERT_TRUE(movereference::write(PRIMARY, NEXT, sizeof(NEXT) - 1));
  EXPECT_EQ(content(history), "prior");
  EXPECT_EQ(content("/.crosspoint/state.json.move-history-0001"), "{\"openEpubPath\":\"/old.epub\"}");
}

TEST_F(MoveReferenceSafeWrite, RefusesUncertainIdentityAndUnexpectedFiles) {
  Storage.failSettingsIdentity();
  EXPECT_FALSE(movereference::write(PRIMARY, NEXT, sizeof(NEXT) - 1));
  EXPECT_EQ(content(PRIMARY), "{\"openEpubPath\":\"/old.epub\"}");
  EXPECT_TRUE(Storage.exists(TEMP));
  EXPECT_FALSE(movereference::write("/.crosspoint/settings.json", NEXT, sizeof(NEXT) - 1));
}
