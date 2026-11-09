#pragma once

#include "ReadingSessionStats.h"

enum class ReadingStatsFrame : uint8_t { Page, Unavailable, EndOfBook };

// One open-book session. No storage or clock calls; caller owns serialization.
class ReadingStatsSession {
 public:
  void onPageRendered(uint32_t nowMs, const ReadingStatsDateTime* localTime, ReadingStatsFrame frame,
                      BookReadingStats& book, GlobalReadingStats& global, uint16_t idleSeconds);
  void noteTurn(bool forward, bool succeeded, uint32_t atMs);
  void pause(uint32_t nowMs, BookReadingStats& book, GlobalReadingStats& global, uint16_t idleSeconds);
  void commit(BookReadingStats& book, GlobalReadingStats& global);
  uint32_t pendingSeconds() const { return seconds; }
  uint32_t previewPendingSeconds(uint16_t idleSeconds) const;
  // Apply a pending eligible forward-page sample to a caller-owned copy.
  void previewPendingPace(BookReadingStats& preview, uint16_t idleSeconds) const;

 private:
  uint32_t eligibleElapsed(uint32_t nowMs, uint16_t idleSeconds) const;
  void accrue(uint32_t nowMs, BookReadingStats& book, GlobalReadingStats& global, uint16_t idleSeconds);
  uint32_t pageShownMs = 0;
  uint32_t seconds = 0;
  uint32_t turnAtMs = 0;
  ReadingStatsDateTime anchorDate{};
  ReadingStatsDate firstDate{};
  std::array<uint32_t, READING_TIME_BUCKET_COUNT> timeBuckets{};
  std::array<uint32_t, READING_DAY_OF_WEEK_COUNT> dayBuckets{};
  uint32_t historyAnchor = 0;
  std::array<uint8_t, READING_HISTORY_BYTES> history{};
  bool hasAnchorDate = false;
  bool pageActive = false;
  bool pendingTurn = false;
  bool pendingForward = false;
};
