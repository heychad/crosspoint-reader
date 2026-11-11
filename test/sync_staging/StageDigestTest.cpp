#include <Memory.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>

#include "network/sync/StageDigest.h"

namespace {
constexpr char NAME[] = "__xsync_0123456789abcdef0123456789abcdef.part";
constexpr char PATH[] = "/__xsync_0123456789abcdef0123456789abcdef.part";
constexpr uint8_t ABC_SHA[] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40,
                               0xde, 0x5d, 0xae, 0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17,
                               0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
bool proceed(void*) { return true; }
bool cancel(void*) { return false; }
bool mutate(void*) {
  ++fake::epoch;
  return true;
}
class DigestTest : public ::testing::Test {
 protected:
  HalStorage storage;
  uint8_t out[32];
  void SetUp() override {
    fake::files.clear();
    fake::epoch = 0;
    fake::failRead = fake::failClose = failAllocation = false;
    auto file = std::make_shared<fake::File>();
    file->bytes = {'a', 'b', 'c'};
    fake::files[PATH] = file;
    std::memset(out, 0x5a, sizeof(out));
  }
  void untouched() {
    for (auto byte : out) EXPECT_EQ(byte, 0x5a);
  }
};
TEST_F(DigestTest, HashesExactBytes) {
  EXPECT_EQ(syncstage::hashStage(storage, NAME, 3, out, proceed, nullptr), syncstage::DigestResult::Ok);
  EXPECT_TRUE(std::equal(std::begin(out), std::end(out), std::begin(ABC_SHA)));
}
TEST_F(DigestTest, RejectsWrongSizeAndReadFailuresWithoutChangingOutput) {
  EXPECT_NE(syncstage::hashStage(storage, NAME, 4, out, proceed, nullptr), syncstage::DigestResult::Ok);
  untouched();
  fake::failRead = true;
  EXPECT_NE(syncstage::hashStage(storage, NAME, 3, out, proceed, nullptr), syncstage::DigestResult::Ok);
  untouched();
}
TEST_F(DigestTest, RejectsMutationCancellationAndCloseFailure) {
  EXPECT_EQ(syncstage::hashStage(storage, NAME, 3, out, mutate, nullptr), syncstage::DigestResult::Changed);
  untouched();
  EXPECT_EQ(syncstage::hashStage(storage, NAME, 3, out, cancel, nullptr), syncstage::DigestResult::Cancelled);
  untouched();
  fake::failClose = true;
  EXPECT_EQ(syncstage::hashStage(storage, NAME, 3, out, proceed, nullptr), syncstage::DigestResult::IoError);
  untouched();
}
TEST_F(DigestTest, RejectsBadNameAndOutOfMemory) {
  EXPECT_EQ(syncstage::hashStage(storage, "/bad", 3, out, proceed, nullptr), syncstage::DigestResult::Invalid);
  untouched();
  failAllocation = true;
  EXPECT_EQ(syncstage::hashStage(storage, NAME, 3, out, proceed, nullptr), syncstage::DigestResult::NoMemory);
  untouched();
}
TEST_F(DigestTest, DestinationHashRequiresAValidVisibleEpubPath) {
  fake::files["/Authors/Book.epub"] = fake::files.at(PATH);
  EXPECT_EQ(syncstage::hashDestination(storage, "/Authors/Book.epub", 3, out, proceed, nullptr),
            syncstage::DigestResult::Ok);
  EXPECT_TRUE(std::equal(std::begin(out), std::end(out), std::begin(ABC_SHA)));
  EXPECT_EQ(syncstage::hashDestination(storage, "/.crosspoint/Book.epub", 3, out, proceed, nullptr),
            syncstage::DigestResult::Invalid);
}
}  // namespace
