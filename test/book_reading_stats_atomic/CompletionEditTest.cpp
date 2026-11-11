#include <BookReadingStats.h>
#include <CompletionEdit.h>
#include <GlobalReadingStats.h>
#include <gtest/gtest.h>

#include <cstdint>

using readingstats::applyCompletion;
using readingstats::CompletionChange;

TEST(CompletionEdit, FinishAndUnfinishAreIdempotent) {
  BookReadingStats book;
  GlobalReadingStats global;
  const ReadingStatsDate today{2026, 11, 6};
  EXPECT_EQ(applyCompletion(book, global, true, today), CompletionChange::Changed);
  EXPECT_TRUE(book.isCompleted);
  EXPECT_EQ(global.completedBooks, 1u);
  EXPECT_EQ(book.finishedDate.year, 2026);
  EXPECT_EQ(book.finishedDate.month, 11);
  EXPECT_EQ(book.finishedDate.day, 6);
  EXPECT_FALSE(book.finishedDateManual);
  EXPECT_EQ(applyCompletion(book, global, true, today), CompletionChange::Unchanged);
  EXPECT_EQ(global.completedBooks, 1u);
  EXPECT_EQ(applyCompletion(book, global, false, today), CompletionChange::Changed);
  EXPECT_FALSE(book.isCompleted);
  EXPECT_EQ(global.completedBooks, 0u);
  EXPECT_EQ(applyCompletion(book, global, false, today), CompletionChange::Unchanged);
}

TEST(CompletionEdit, KeepsExistingOrManualFinishDates) {
  BookReadingStats book;
  GlobalReadingStats global;
  book.finishedDate = {2023, 2, 4};
  book.finishedDateManual = true;
  EXPECT_EQ(applyCompletion(book, global, true, {2026, 11, 6}), CompletionChange::Changed);
  EXPECT_EQ(book.finishedDate.year, 2023);
  EXPECT_TRUE(book.finishedDateManual);
  EXPECT_EQ(applyCompletion(book, global, false, {2026, 11, 6}), CompletionChange::Changed);
  EXPECT_EQ(book.finishedDate.year, 2023);
  EXPECT_TRUE(book.finishedDateManual);
}

TEST(CompletionEdit, MissingClockLeavesDateUnset) {
  BookReadingStats book;
  GlobalReadingStats global;
  EXPECT_EQ(applyCompletion(book, global, true, {}), CompletionChange::Changed);
  EXPECT_FALSE(book.finishedDate.isValid());
}

TEST(CompletionEdit, UnwritableInputsRemainUntouched) {
  for (const bool bookWritable : {false, true}) {
    BookReadingStats book;
    GlobalReadingStats global;
    book.writable = bookWritable;
    global.writable = !bookWritable;
    EXPECT_EQ(applyCompletion(book, global, true, {2026, 11, 6}), CompletionChange::Invalid);
    EXPECT_FALSE(book.isCompleted);
    EXPECT_EQ(global.completedBooks, 0u);
    EXPECT_FALSE(book.finishedDate.isValid());
  }
}

TEST(CompletionEdit, CountSaturatesAndNeverUnderflows) {
  BookReadingStats book;
  GlobalReadingStats global;
  global.completedBooks = UINT32_MAX;
  EXPECT_EQ(applyCompletion(book, global, true, {}), CompletionChange::Changed);
  EXPECT_EQ(global.completedBooks, UINT32_MAX);
  global.completedBooks = 0;
  EXPECT_EQ(applyCompletion(book, global, false, {}), CompletionChange::Changed);
  EXPECT_EQ(global.completedBooks, 0u);
}
