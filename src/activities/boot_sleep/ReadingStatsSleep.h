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
}  // namespace sleepstats
