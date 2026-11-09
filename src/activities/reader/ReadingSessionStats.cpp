// Adapted from CrossInk v1.6.1; see docs/downstream/CROSSINK_NOTICE.md.
#include "ReadingSessionStats.h"

#include <limits>

void commitReadingSession(BookReadingStats& book, GlobalReadingStats& global, const uint32_t seconds,
                          const ReadingStatsDateTime* sessionStart) {
  // Page intervals longer than the idle threshold are rejected before they
  // reach the session total, so seconds is active reading time.
  if (seconds >= 60) {
    if (book.sessionCount < UINT16_MAX) ++book.sessionCount;
    if (global.totalSessions < UINT32_MAX) ++global.totalSessions;
  }
  if (seconds < 10) return;
  book.totalReadingSeconds =
      UINT32_MAX - book.totalReadingSeconds < seconds ? UINT32_MAX : book.totalReadingSeconds + seconds;
  global.totalReadingSeconds =
      UINT32_MAX - global.totalReadingSeconds < seconds ? UINT32_MAX : global.totalReadingSeconds + seconds;
  if (!sessionStart) return;
  book.recordReadingSpan(*sessionStart, seconds);
  global.recordReadingSpan(*sessionStart, seconds);
  if (seconds >= 120 && !book.startDateManual && !book.startDate.isValid()) {
    book.startDate = sessionStart->date;
  }
}
