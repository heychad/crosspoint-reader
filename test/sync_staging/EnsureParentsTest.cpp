#include <Memory.h>
#include <gtest/gtest.h>

#include "network/sync/EnsureParents.h"

namespace {
using namespace syncpublish;
bool proceed(void*) { return true; }
bool cancel(void*) { return false; }
bool mutate(void*) {
  ++fake::epoch;
  return true;
}
class ParentTest : public ::testing::Test {
 protected:
  HalStorage storage;
  void SetUp() override {
    fake::files.clear();
    fake::directories = {"/", "/.crosspoint", "/Authors"};
    fake::epoch = 0;
    fake::writers = 0;
    fake::failWrite = fake::failClose = fake::failRead = fake::failSync = fake::missingParent = fake::failMkdir =
        failAllocation = false;
  }
};
TEST_F(ParentTest, CreatesOnlyMissingParentsAndIsIdempotent) {
  const auto first = ensureParents(storage, "/Authors/Writer/Series/Book.epub", proceed, nullptr);
  EXPECT_EQ(first.state, ParentState::Ready);
  EXPECT_EQ(first.created, 2u);
  EXPECT_TRUE(fake::directories.count("/Authors/Writer"));
  EXPECT_TRUE(fake::directories.count("/Authors/Writer/Series"));
  EXPECT_FALSE(fake::files.count("/Authors/Writer/Series/Book.epub"));
  const auto before = fake::epoch;
  const auto again = ensureParents(storage, "/Authors/Writer/Series/Book.epub", proceed, nullptr);
  EXPECT_EQ(again.state, ParentState::Ready);
  EXPECT_EQ(again.created, 0u);
  EXPECT_EQ(fake::epoch, before);
}
TEST_F(ParentTest, RefusesFilesInvalidPathsAndStorageFailures) {
  fake::files["/Authors/Writer"] = std::make_shared<fake::File>();
  EXPECT_EQ(ensureParents(storage, "/Authors/Writer/Book.epub", proceed, nullptr).state, ParentState::Conflict);
  EXPECT_EQ(ensureParents(storage, "/Authors/../Book.epub", proceed, nullptr).state, ParentState::Invalid);
  fake::files.clear();
  fake::failMkdir = true;
  EXPECT_EQ(ensureParents(storage, "/Authors/Writer/Book.epub", proceed, nullptr).state, ParentState::Uncertain);
  EXPECT_FALSE(fake::directories.count("/Authors/Writer"));
}
TEST_F(ParentTest, RespectsCancellationAndInterveningWrites) {
  EXPECT_EQ(ensureParents(storage, "/Authors/Writer/Book.epub", cancel, nullptr).state, ParentState::Cancelled);
  EXPECT_EQ(ensureParents(storage, "/Authors/Writer/Book.epub", mutate, nullptr).state, ParentState::Changed);
  EXPECT_FALSE(fake::directories.count("/Authors/Writer"));
}
}  // namespace
