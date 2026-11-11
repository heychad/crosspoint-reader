#include <BookReadingStats.h>
#include <CompletionSnapshotValidation.h>
#include <GlobalReadingStats.h>
#include <gtest/gtest.h>

namespace cj = readingstats::completionjournal;

TEST(CompletionSnapshotValidation, AcceptsCurrentEncodedStatistics) {
  BookReadingStats book;
  book.isCompleted = true;
  book.startDate = {2024, 2, 29};
  GlobalReadingStats global;
  cj::Book bookBytes{};
  cj::Global globalBytes{};
  ASSERT_TRUE(book.encode(bookBytes.data(), bookBytes.size()));
  ASSERT_TRUE(global.encode(globalBytes));
  EXPECT_TRUE(cj::validSnapshotPayload(bookBytes.data(), globalBytes.data()));
}

TEST(CompletionSnapshotValidation, RejectsInvalidVersionsFlagsAndDates) {
  BookReadingStats book;
  GlobalReadingStats global;
  cj::Book bookBytes{};
  cj::Global globalBytes{};
  ASSERT_TRUE(book.encode(bookBytes.data(), bookBytes.size()));
  ASSERT_TRUE(global.encode(globalBytes));
  for (const auto index : {0u, 11u, 16u, 17u, 19u, 20u, 21u, 23u, 24u}) {
    auto damaged = bookBytes;
    damaged[index] = 0xff;
    EXPECT_FALSE(cj::validSnapshotPayload(damaged.data(), globalBytes.data())) << index;
  }
  globalBytes[0] = 0xff;
  EXPECT_FALSE(cj::validSnapshotPayload(bookBytes.data(), globalBytes.data()));
  EXPECT_FALSE(cj::validSnapshotPayload(nullptr, globalBytes.data()));
  EXPECT_FALSE(cj::validSnapshotPayload(bookBytes.data(), nullptr));
}
