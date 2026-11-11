#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>

#include "network/sync/PublishIntent.h"

namespace {
constexpr char STAGE[] = "__xsync_0123456789abcdef0123456789abcdef.part";
TEST(PublishIntentTest, RoundTripPreservesDestinationDigestAndSize) {
  syncpublish::Digest digest{};
  for (size_t i = 0; i < digest.size(); ++i) digest[i] = i;
  syncpublish::IntentView input{STAGE, "/Authors/José/Book.EPUB", 12345, digest}, output{};
  syncpublish::Record record{};
  size_t length = 0;
  ASSERT_TRUE(syncpublish::encode(input, record, length));
  ASSERT_TRUE(syncpublish::decode(record.data(), length, output));
  EXPECT_EQ(output.stage, input.stage);
  EXPECT_EQ(output.destination, input.destination);
  EXPECT_EQ(output.digest, digest);
  EXPECT_EQ(output.size, input.size);
}
TEST(PublishIntentTest, MatchesPriorRecordLayoutAndKnownChecksum) {
  syncpublish::IntentView input{STAGE, "/Authors/Book.epub", 3, {}};
  syncpublish::Record record{};
  size_t length = 0;
  ASSERT_TRUE(syncpublish::encode(input, record, length));
  ASSERT_EQ(length, 113u);
  EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(record.data()), 4), "XSP1");
  EXPECT_EQ(record[4], 3);
  EXPECT_EQ(record[12], 18);
  EXPECT_EQ(record[length - 4], 0x70);
  EXPECT_EQ(record[length - 3], 0xd7);
  EXPECT_EQ(record[length - 2], 0x2f);
  EXPECT_EQ(record[length - 1], 0xe5);
}
TEST(PublishIntentTest, CorruptionAndTruncationLeaveTheOutputUnchanged) {
  syncpublish::IntentView input{STAGE, "/Authors/Book.epub", 3, {}}, output{"sentinel", "sentinel", 99, {}};
  syncpublish::Record record{};
  size_t length = 0;
  ASSERT_TRUE(syncpublish::encode(input, record, length));
  for (size_t n = 0; n < length; ++n) EXPECT_FALSE(syncpublish::decode(record.data(), n, output));
  for (size_t i = 0; i < length; ++i) {
    auto changed = record;
    changed[i] ^= 1;
    EXPECT_FALSE(syncpublish::decode(changed.data(), length, output));
  }
  EXPECT_EQ(output.stage, "sentinel");
  EXPECT_EQ(output.destination, "sentinel");
  EXPECT_EQ(output.size, 99u);
}
TEST(PublishIntentTest, RejectsUnsafeDestinationsAndNonEpubs) {
  for (const auto* path : {"Authors/Book.epub", "/.crosspoint/Book.epub", "/XTCache/Book.epub", "/Authors//Book.epub",
                           "/Authors/../Book.epub", "/Authors/BOOK~1.epub", "/Authors/Book.epub.", "/Authors/Book.pdf",
                           "/Authors/Book\\Bad.epub", "/Authors/Book:Bad.epub", "/Authors/Bad\x7f.epub"})
    EXPECT_FALSE(syncpublish::validDestination(path)) << path;
  std::string bad = "/Authors/Bad";
  bad.push_back('\0');
  bad += ".epub";
  EXPECT_FALSE(syncpublish::validDestination(bad));
  EXPECT_TRUE(syncpublish::validDestination("/Anthologies/Book.epub"));
}
TEST(PublishIntentTest, BoundsAndFailureDoNotAlterOutput) {
  syncpublish::Record record;
  record.fill(0x5a);
  size_t length = 77;
  syncpublish::IntentView bad{"bad", "/Authors/Book.epub", 0, {}};
  EXPECT_FALSE(syncpublish::encode(bad, record, length));
  EXPECT_EQ(length, 77u);
  EXPECT_TRUE(std::all_of(record.begin(), record.end(), [](uint8_t b) { return b == 0x5a; }));
  bad.stage = STAGE;
  bad.size = syncstage::MAX_STAGE_BYTES + 1;
  EXPECT_FALSE(syncpublish::encode(bad, record, length));
  std::string deep;
  for (int i = 0; i < 16; ++i) deep += "/a";
  deep += "/book.epub";
  EXPECT_FALSE(syncpublish::validDestination(deep));
}
}  // namespace
