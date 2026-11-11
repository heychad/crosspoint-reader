#pragma once

#include "activities/reader/BookReadingStats.h"
#include "activities/reader/GlobalReadingStats.h"

class GfxRenderer;
class Bitmap;

namespace sleepstats {
struct Data {
  BookReadingStats book;
  GlobalReadingStats device;
  ReadingStatsDateTime now;
  int progress = -1;  // Whole-book percent, or -1 when unavailable.
  bool hasClock = false;
  bool hasDevice = false;
};

// Draws a complete monochrome frame, or returns false when the display is too small.
bool draw(GfxRenderer& renderer, const char* title, const Data& data);
bool drawSummary(GfxRenderer& renderer, const char* title, const Data& data);
bool drawCoverSummary(GfxRenderer& renderer, const char* title, const Data& data, const Bitmap& cover);
bool drawMinimalBook(GfxRenderer& renderer, const char* title, int progress, uint32_t readingSeconds,
                     const Bitmap* cover);
struct DashboardData {
  int progress = -1;
  uint32_t readingSeconds = 0;
  uint32_t remainingSeconds = 0;
  uint32_t sessions = 0;
  uint32_t pages = 0;
  uint32_t globalSeconds = 0;
  uint32_t completedBooks = 0;
  uint32_t dailyAverageSeconds = 0;
  ReadingStatsDate startDate;
  uint16_t currentStreak = 0;
  bool hasStats = false;
  bool hasGlobal = false;
  bool hasDailyAverage = false;
  bool hasStreak = false;
};
bool drawBookDashboard(GfxRenderer& renderer, const char* title, const char* author, const DashboardData& data,
                       const Bitmap* cover);
}  // namespace sleepstats
