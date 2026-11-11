#include "ReadingStatsSession.h"

#include <algorithm>

namespace {
void add(uint32_t& value, uint32_t amount) { value = UINT32_MAX - value < amount ? UINT32_MAX : value + amount; }
}  // namespace

void ReadingStatsSession::noteTurn(bool forward, bool succeeded, uint32_t atMs) {
  turnAtMs = atMs;
  pendingTurn = succeeded;
  pendingForward = forward && succeeded;
}

uint32_t ReadingStatsSession::eligibleElapsed(uint32_t nowMs, uint16_t idleSeconds) const {
  if (!pageActive) return 0;
  const uint32_t elapsed = (nowMs - pageShownMs) / 1000U;
  return elapsed > idleSeconds ? 0 : elapsed;
}

uint32_t ReadingStatsSession::previewPendingSeconds(uint16_t idleSeconds) const {
  uint32_t preview = seconds;
  if (pendingTurn) add(preview, eligibleElapsed(turnAtMs, idleSeconds));
  return preview;
}

void ReadingStatsSession::previewPendingPace(BookReadingStats& preview, uint16_t idleSeconds) const {
  if (!pendingTurn || !pendingForward) return;
  const uint32_t elapsed = eligibleElapsed(turnAtMs, idleSeconds);
  if (elapsed >= 2) preview.recordForwardPageRead(elapsed);
}

void ReadingStatsSession::accrue(uint32_t nowMs, BookReadingStats& book, GlobalReadingStats& global,
                                 uint16_t idleSeconds) {
  const uint32_t elapsed = eligibleElapsed(nowMs, idleSeconds);
  if (elapsed == 0) return;
  add(seconds, elapsed);
#if defined(CROSSPOINT_EXPERIMENTAL_DAILY_STATS)
  daily.add(hasAnchorDate ? &anchorDate : nullptr, elapsed, seconds);
#endif
  if (hasAnchorDate) {
    if (!firstDate.isValid()) firstDate = anchorDate.date;
    recordReadingSpanIntoBuckets(timeBuckets, dayBuckets, anchorDate, elapsed);
    recordReadingSpanIntoHistory(historyAnchor, history, anchorDate, elapsed);
  }
  if (pendingForward && elapsed >= 2) {
    add(book.totalPagesTurned, 1);
    add(global.totalPagesTurned, 1);
    book.recordForwardPageRead(elapsed);
  }
}

void ReadingStatsSession::onPageRendered(uint32_t nowMs, const ReadingStatsDateTime* localTime, ReadingStatsFrame frame,
                                         BookReadingStats& book, GlobalReadingStats& global, uint16_t idleSeconds) {
  if (frame != ReadingStatsFrame::Page) {
    if (frame == ReadingStatsFrame::EndOfBook && pendingTurn) {
      accrue(turnAtMs, book, global, idleSeconds);
      pageActive = false;
      pendingTurn = false;
      pendingForward = false;
    } else {
      pause(nowMs, book, global, idleSeconds);
    }
    return;
  }
  if (pendingTurn) accrue(turnAtMs, book, global, idleSeconds);
  if (!pageActive || pendingTurn) {
    pageShownMs = nowMs;
    pageActive = true;
    hasAnchorDate = localTime && localTime->isValid();
    if (hasAnchorDate) anchorDate = *localTime;
  }
  pendingTurn = false;
  pendingForward = false;
}

void ReadingStatsSession::pause(uint32_t nowMs, BookReadingStats& book, GlobalReadingStats& global,
                                uint16_t idleSeconds) {
  pendingForward = false;
  accrue(nowMs, book, global, idleSeconds);
  pageActive = false;
  pendingTurn = false;
  pendingForward = false;
}

void ReadingStatsSession::preview(BookReadingStats& book, GlobalReadingStats& global) const {
  commitReadingSession(book, global, seconds, nullptr);
  if (seconds >= 10) {
    for (size_t i = 0; i < timeBuckets.size(); ++i) {
      add(book.timeOfDaySeconds[i], timeBuckets[i]);
      add(global.timeOfDaySeconds[i], timeBuckets[i]);
    }
    for (size_t i = 0; i < dayBuckets.size(); ++i) {
      add(book.dayOfWeekSeconds[i], dayBuckets[i]);
      add(global.dayOfWeekSeconds[i], dayBuckets[i]);
    }
    mergeReadingHistory(global.readingHistoryAnchorDay, global.readingHistoryBits, historyAnchor, history);
    global.longestReadingStreak = global.displayLongestReadingStreak();
    if (seconds >= 120 && !book.startDateManual && !book.startDate.isValid()) book.startDate = firstDate;
  }
}

void ReadingStatsSession::commit(BookReadingStats& book, GlobalReadingStats& global) {
  preview(book, global);
#if defined(CROSSPOINT_EXPERIMENTAL_DAILY_STATS)
  daily.endSession();
#endif
  seconds = 0;
  firstDate = {};
  timeBuckets.fill(0);
  dayBuckets.fill(0);
  historyAnchor = 0;
  history.fill(0);
}
