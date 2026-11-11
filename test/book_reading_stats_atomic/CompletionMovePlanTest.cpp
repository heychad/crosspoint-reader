#include <CompletionMovePlan.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace cm = readingstats::completionmove;

TEST(CompletionMovePlan, BindsPathsToCompletionAndDetectsDamage) {
  auto plan = std::make_unique<cm::Plan>();
  size_t size = 0;
  constexpr uint64_t hash = 0x0123456789abcdefULL;
  ASSERT_TRUE(cm::encode("/Authors/Writer/Book.epub", "/read/Book (2).epub", hash, *plan, size));
  std::string_view from, to;
  ASSERT_TRUE(cm::decode(plan->data(), size, hash, from, to));
  EXPECT_EQ(from, "/Authors/Writer/Book.epub");
  EXPECT_EQ(to, "/read/Book (2).epub");
  EXPECT_FALSE(cm::decode(plan->data(), size, hash ^ (uint64_t{1} << 63), from, to));
  for (size_t i = 0; i < size; ++i) {
    auto damaged = *plan;
    damaged[i] ^= 1;
    EXPECT_FALSE(cm::decode(damaged.data(), size, hash, from, to)) << i;
    EXPECT_FALSE(cm::decode(plan->data(), i, hash, from, to)) << i;
  }
}

TEST(CompletionMovePlan, RejectsAmbiguousAndOutOfBoundsPaths) {
  auto plan = std::make_unique<cm::Plan>();
  size_t size = 10;
  for (const auto path : {"", "relative.epub", "/", "/a//b", "/a/../b", "/a/./b", "/a/", "/a\\b", "/a/\nb"}) {
    EXPECT_FALSE(cm::encode(path, "/read/book.epub", 1, *plan, size)) << path;
    EXPECT_EQ(size, 0u);
  }
  EXPECT_FALSE(cm::encode("/a.epub", "/read/../a.epub", 1, *plan, size));
  EXPECT_FALSE(cm::encode("/a.epub", "/other/a.epub", 1, *plan, size));
  EXPECT_FALSE(cm::encode("/a.epub", "/read/nested/a.epub", 1, *plan, size));
  EXPECT_FALSE(cm::encode("/read/a.epub", "/read/a.epub", 1, *plan, size));
  EXPECT_FALSE(cm::encode("/" + std::string(cm::MAX_PATH_BYTES, 'x'), "/read/a.epub", 1, *plan, size));
  EXPECT_TRUE(cm::encode("/" + std::string(cm::MAX_PATH_BYTES - 1, 'x'), "/read/a.epub", 1, *plan, size));
}
