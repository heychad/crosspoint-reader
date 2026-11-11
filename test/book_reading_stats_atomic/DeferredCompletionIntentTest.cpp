#include <DeferredCompletionIntent.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace dc = readingstats::deferredcompletion;

TEST(DeferredCompletionIntent, BindsBookAndCompletionAndRejectsDamage) {
  auto record = std::make_unique<dc::Record>();
  size_t size = 0;
  constexpr uint64_t completionHash = 0x0123456789abcdefULL;
  ASSERT_TRUE(dc::encode("/Authors/Ursula Le Guin/Le monde.epub", completionHash, *record, size));
  std::string_view source = "unchanged";
  ASSERT_TRUE(dc::decode(record->data(), size, completionHash, source));
  EXPECT_EQ(source, "/Authors/Ursula Le Guin/Le monde.epub");
  source = "unchanged";
  EXPECT_FALSE(dc::decode(record->data(), size, completionHash ^ 1, source));
  EXPECT_EQ(source, "unchanged");
  for (size_t i = 0; i < size; ++i) {
    auto damaged = *record;
    damaged[i] ^= 1;
    EXPECT_FALSE(dc::decode(damaged.data(), size, completionHash, source)) << i;
    EXPECT_FALSE(dc::decode(record->data(), i, completionHash, source)) << i;
    EXPECT_EQ(source, "unchanged");
  }
}

TEST(DeferredCompletionIntent, ChecksPathBoundsAndPreservesUnicode) {
  auto record = std::make_unique<dc::Record>();
  size_t size = 1;
  for (const auto path : {"", "relative.epub", "/", "/a//b", "/a/../b", "/a/./b", "/a/", "/a\\b", "/a/\nb"}) {
    EXPECT_FALSE(dc::encode(path, 1, *record, size)) << path;
    EXPECT_EQ(size, 0u);
  }
  EXPECT_FALSE(dc::encode("/" + std::string(readingstats::completionmove::MAX_PATH_BYTES, 'x'), 1, *record, size));
  const std::string path = "/" + std::string(readingstats::completionmove::MAX_PATH_BYTES - 1, 'x');
  ASSERT_TRUE(dc::encode(path, 1, *record, size));
  std::string_view decoded;
  ASSERT_TRUE(dc::decode(record->data(), size, 1, decoded));
  EXPECT_EQ(decoded, path);
  ASSERT_TRUE(dc::encode("/Écrivain/読書.epub", 1, *record, size));
  ASSERT_TRUE(dc::decode(record->data(), size, 1, decoded));
  EXPECT_EQ(decoded, "/Écrivain/読書.epub");
}
