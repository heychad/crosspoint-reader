#include <Memory.h>
#include <gtest/gtest.h>

#include "network/sync/CanonicalDestination.h"

namespace {
bool proceed(void*) { return true; }
bool cancel(void*) { return false; }
bool mutate(void*) {
  ++fake::epoch;
  return true;
}
class CanonicalTest : public ::testing::Test {
 protected:
  HalStorage storage;
  void SetUp() override {
    fake::files.clear();
    fake::directories = {"/", "/.crosspoint", "/Authors", "/Authors/Writer"};
    fake::epoch = 0;
    fake::writers = 0;
    fake::failRead = fake::failClose = failAllocation = false;
  }
};
TEST_F(CanonicalTest, RequiresExistingParentsAndRejectsTemporarySiblings) {
  using namespace syncpublish;
  const char* path = "/Authors/Writer/Book.epub";
  EXPECT_EQ(inspectCanonicalDestination(storage, path, proceed, nullptr), CanonicalResult::Clear);
  fake::files["/Authors/Writer/other.epub.part"] = std::make_shared<fake::File>();
  EXPECT_EQ(inspectCanonicalDestination(storage, path, proceed, nullptr), CanonicalResult::Conflict);
  fake::files.clear();
  fake::directories.erase("/Authors/Writer");
  EXPECT_EQ(inspectCanonicalDestination(storage, path, proceed, nullptr), CanonicalResult::MissingParent);
}
TEST_F(CanonicalTest, AllowsGeneratedRootStageButRejectsOtherRootTemporaries) {
  using namespace syncpublish;
  fake::files["/__xsync_0123456789abcdef0123456789abcdef.part"] = std::make_shared<fake::File>();
  EXPECT_EQ(inspectCanonicalDestination(storage, "/Book.epub", proceed, nullptr), CanonicalResult::Clear);
  fake::files["/other.part"] = std::make_shared<fake::File>();
  EXPECT_EQ(inspectCanonicalDestination(storage, "/Book.epub", proceed, nullptr), CanonicalResult::Conflict);
}
TEST_F(CanonicalTest, CancellationAndMutationAreNotClear) {
  using namespace syncpublish;
  EXPECT_EQ(inspectCanonicalDestination(storage, "/Book.epub", cancel, nullptr), CanonicalResult::Cancelled);
  EXPECT_EQ(inspectCanonicalDestination(storage, "/Book.epub", mutate, nullptr), CanonicalResult::Changed);
  EXPECT_EQ(inspectCanonicalDestination(storage, "/BOOK~1/Book.epub", proceed, nullptr), CanonicalResult::Invalid);
}
}  // namespace
