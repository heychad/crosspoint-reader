#include "CompletionEdit.h"

#include <cstdint>

#include "BookReadingStats.h"
#include "GlobalReadingStats.h"
#include "ReadingStatsUtils.h"

readingstats::CompletionChange readingstats::applyCompletion(BookReadingStats& book, GlobalReadingStats& global,
                                                             const bool completed, const ReadingStatsDate& today) {
  if (!book.writable || !global.writable) return CompletionChange::Invalid;
  if (book.isCompleted == completed) return CompletionChange::Unchanged;
  book.isCompleted = completed;
  if (completed) {
    if (global.completedBooks < UINT32_MAX) ++global.completedBooks;
    if (!book.finishedDateManual && !book.finishedDate.isValid() && today.isValid()) book.finishedDate = today;
  } else if (global.completedBooks > 0) {
    --global.completedBooks;
  }
  return CompletionChange::Changed;
}
