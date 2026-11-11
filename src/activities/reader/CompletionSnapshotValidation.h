#pragma once

#include "BookReadingStats.h"
#include "CompletionJournal.h"
#include "GlobalReadingStats.h"

namespace readingstats::completionjournal {
// Replays are limited to the current formats produced by the local encoders.
// A valid envelope alone does not establish that its payload can be used.
inline bool validSnapshotDate(const uint8_t* bytes) {
  ReadingStatsDate date;
  date.year = static_cast<uint16_t>(bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8));
  date.month = bytes[2];
  date.day = bytes[3];
  return (!date.year && !date.month && !date.day) || date.isValid();
}

inline bool validSnapshotPayload(const uint8_t* book, const uint8_t* global) {
  if (!book || !global) return false;
  static_assert(BOOK_BYTES == BookReadingStats::CURRENT_FILE_SIZE);
  static_assert(GLOBAL_BYTES == GlobalReadingStats::CURRENT_FILE_SIZE);
  return book[0] == BookReadingStats::CURRENT_FILE_VERSION && book[11] <= 1 && (book[16] & ~uint8_t{3}) == 0 &&
         validSnapshotDate(book + 17) && validSnapshotDate(book + 21) &&
         global[0] == GlobalReadingStats::CURRENT_FILE_VERSION;
}
}  // namespace readingstats::completionjournal
