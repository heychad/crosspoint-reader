#include <HalStorage.h>
#include <Memory.h>
#include <gtest/gtest.h>

#include "activities/reader/CompletionArchiveInventory.h"

namespace cj = readingstats::completionjournal;
namespace {
class ArchiveInventoryTest : public ::testing::Test {
 protected:
  HalStorage storage;
  void SetUp() override {
    fake::files.clear();
    fake::directories = {"/", "/Authors", "/.completion-history-000000", "/.completion-history-000002"};
    fake::epoch = 0;
    fake::writers = 0;
    fake::failRead = fake::failClose = failAllocation = false;
  }
};

TEST_F(ArchiveInventoryTest, FindsArchivesAcrossGaps) {
  cj::ArchiveInventory inventory{};
  ASSERT_EQ(cj::inventoryArchives(storage, inventory), cj::ArchiveInventoryResult::Ready);
  EXPECT_TRUE(cj::hasArchive(inventory, 0));
  EXPECT_FALSE(cj::hasArchive(inventory, 1));
  EXPECT_TRUE(cj::hasArchive(inventory, 2));
  EXPECT_FALSE(cj::hasArchive(inventory, cj::MAX_ARCHIVE_INDEX));
}

TEST_F(ArchiveInventoryTest, RejectsNoncanonicalNamesAndLeavesOutputUntouched) {
  cj::ArchiveInventory inventory{};
  inventory.fill(0xa5);
  fake::directories.insert("/.COMPLETION-HISTORY-000003");
  EXPECT_EQ(cj::inventoryArchives(storage, inventory), cj::ArchiveInventoryResult::Conflict);
  EXPECT_EQ(inventory[0], 0xa5);
  fake::directories.erase("/.COMPLETION-HISTORY-000003");
  fake::files["/.completion-history-000003"] = std::make_shared<fake::File>();
  EXPECT_EQ(cj::inventoryArchives(storage, inventory), cj::ArchiveInventoryResult::Conflict);
  EXPECT_EQ(inventory[0], 0xa5);
}

TEST_F(ArchiveInventoryTest, RefusesOutOfRangeAndAllocationFailure) {
  cj::ArchiveInventory inventory{};
  inventory.fill(0xa5);
  fake::directories.insert("/.completion-history-001024");
  EXPECT_EQ(cj::inventoryArchives(storage, inventory), cj::ArchiveInventoryResult::Unavailable);
  EXPECT_EQ(inventory[0], 0xa5);
  fake::directories.erase("/.completion-history-001024");
  failAllocation = true;
  EXPECT_EQ(cj::inventoryArchives(storage, inventory), cj::ArchiveInventoryResult::Unavailable);
  EXPECT_EQ(inventory[0], 0xa5);
  failAllocation = false;
  fake::writers = 1;
  EXPECT_EQ(cj::inventoryArchives(storage, inventory), cj::ArchiveInventoryResult::Unavailable);
  EXPECT_EQ(inventory[0], 0xa5);
}
}  // namespace
