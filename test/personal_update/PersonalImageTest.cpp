#include <PersonalImage.h>
#include <gtest/gtest.h>

#include <limits>

#include "ImageFixture.h"
namespace {
using personalupdate::ImageResult;
using personalupdate::ImageVerifier;
class PersonalImage : public testing::Test {
 protected:
  void SetUp() override { fakehash::failStart = fakehash::failUpdate = fakehash::failFinish = false; }
};
TEST_F(PersonalImage, VerifiesAllChunkWidthsAndExplicitMatchingBoard) {
  const auto image = imageFixture();
  const auto digest = imageDigest(image);
  for (size_t width = 1; width <= image.size() + 1; ++width) {
    SCOPED_TRACE(width);
    ImageVerifier verifier(image.size(), digest, 9);
    for (size_t i = 0; i < image.size(); i += width)
      ASSERT_TRUE(verifier.feed(image.data() + i, std::min(width, image.size() - i)));
    EXPECT_EQ(verifier.received(), image.size());
    EXPECT_EQ(verifier.header().size(), 14u);
    EXPECT_EQ(verifier.finish(), ImageResult::Verified);
    EXPECT_EQ(verifier.finish(), ImageResult::Verified);
  }
}
TEST_F(PersonalImage, RejectsEveryTruncatedImageAndAnOversizedChunk) {
  const auto image = imageFixture();
  const auto digest = imageDigest(image);
  for (size_t length = 0; length < image.size(); ++length) {
    ImageVerifier verifier(image.size(), digest, 9);
    ASSERT_TRUE(verifier.feed(image.data(), length));
    EXPECT_EQ(verifier.finish(), ImageResult::TooShort);
  }
  ImageVerifier oversized(image.size(), digest, 9);
  EXPECT_FALSE(oversized.feed(image.data(), std::numeric_limits<size_t>::max()));
  EXPECT_EQ(oversized.result(), ImageResult::TooLong);
}
TEST_F(PersonalImage, RefusesWrongMagicChipMissingAndConflictingTagsEvenWithMatchingHashes) {
  for (int defect = 0; defect < 4; ++defect) {
    auto image = imageFixture();
    if (defect == 0)
      image[0] = 0;
    else if (defect == 1)
      image[12] = 0;
    else if (defect == 2)
      std::fill(image.begin() + 40, image.end(), 0xa5);
    else {
      const std::string tag = "CROSSPOINT-BOARD-V1:sticky;";
      std::memcpy(image.data() + 40, tag.data(), tag.size());
    }
    ImageVerifier verifier(image.size(), imageDigest(image), 9);
    verifier.feed(image.data(), image.size());
    EXPECT_EQ(verifier.finish(), defect == 0 ? ImageResult::InvalidHeader : ImageResult::WrongDevice);
  }
  auto image = imageFixture();
  const std::string wrong = "CROSSPOINT-BOARD-V1:x4c;";
  std::memcpy(image.data() + 80, wrong.data(), wrong.size());
  ImageVerifier conflict(image.size(), imageDigest(image), 9);
  EXPECT_FALSE(conflict.feed(image.data(), image.size()));
  EXPECT_EQ(conflict.result(), ImageResult::WrongDevice);
}
TEST_F(PersonalImage, RejectsSameSizeCorruptionAndBytesAfterVerification) {
  auto image = imageFixture();
  const auto digest = imageDigest(image);
  image.back() ^= 1;
  ImageVerifier damaged(image.size(), digest, 9);
  ASSERT_TRUE(damaged.feed(image.data(), image.size()));
  EXPECT_EQ(damaged.finish(), ImageResult::DigestMismatch);
  ImageVerifier complete(image.size(), imageDigest(image), 9);
  ASSERT_TRUE(complete.feed(image.data(), image.size()));
  ASSERT_EQ(complete.finish(), ImageResult::Verified);
  EXPECT_FALSE(complete.feed(image.data(), 1));
  EXPECT_EQ(complete.finish(), ImageResult::TooLong);
}
TEST_F(PersonalImage, HandlesHashFailuresInvalidMetadataAndNullData) {
  const auto image = imageFixture();
  const auto digest = imageDigest(image);
  {
    ImageVerifier invalid(23, digest, 9);
    EXPECT_EQ(invalid.result(), ImageResult::InvalidMetadata);
  }
  {
    ImageVerifier unknown(image.size(), digest, 0xffff);
    EXPECT_EQ(unknown.result(), ImageResult::WrongDevice);
  }
  fakehash::failStart = true;
  {
    ImageVerifier failed(image.size(), digest, 9);
    EXPECT_EQ(failed.result(), ImageResult::HashError);
  }
  fakehash::failStart = false;
  {
    ImageVerifier failed(image.size(), digest, 9);
    fakehash::failUpdate = true;
    EXPECT_FALSE(failed.feed(image.data(), image.size()));
    EXPECT_EQ(failed.result(), ImageResult::HashError);
  }
  fakehash::failUpdate = false;
  {
    ImageVerifier failed(image.size(), digest, 9);
    ASSERT_TRUE(failed.feed(image.data(), image.size()));
    fakehash::failFinish = true;
    EXPECT_EQ(failed.finish(), ImageResult::HashError);
  }
  fakehash::failFinish = false;
  {
    ImageVerifier empty(image.size(), digest, 9);
    EXPECT_TRUE(empty.feed(nullptr, 0));
    EXPECT_FALSE(empty.feed(nullptr, 1));
    EXPECT_EQ(empty.result(), ImageResult::InvalidHeader);
  }
}
TEST_F(PersonalImage, LegacyScannerStillPermitsAnUntaggedImageButReportsPresenceSeparately) {
  const auto untagged = imageFixture("");
  board_tag::Scanner scanner;
  scanner.feed(untagged.data(), untagged.size());
  EXPECT_FALSE(scanner.mismatch());
  EXPECT_FALSE(scanner.matched());
  const auto tagged = imageFixture();
  scanner.feed(tagged.data(), tagged.size());
  EXPECT_TRUE(scanner.matched());
  EXPECT_FALSE(scanner.mismatch());
}
}  // namespace
