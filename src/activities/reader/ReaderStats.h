#pragma once

#include <string>

#include "ReadingStatsSession.h"

// Reader-lifetime state; all access is under the existing render/transition lock.
class ReaderStats {
 public:
  void begin(const std::string& cachePath, const std::string& bookPath, bool trackingEnabled, uint16_t idleSeconds);
  void noteTurn(bool forward, bool succeeded);
  void onRender(ReadingStatsFrame frame);
  void pause();

  // True only when enabled tracking could not safely load its state.
  bool unavailableAtStart() const { return startUnavailable; }
  bool finish();
  bool snapshot(BookReadingStats& bookOut, GlobalReadingStats& globalOut, bool& saved);
  // Apply an explicit date override while retaining the current session.
  bool editBookDate(bool started, const ReadingStatsDate& date, BookReadingStats& bookOut,
                    GlobalReadingStats& globalOut, bool& saved);
  // Zero means insufficient tracking data or an unavailable estimate.
  uint32_t bookTimeLeft(int progressBasisPoints) const;
  // Estimate remaining whole pages in the current EPUB section; zero is unavailable.
  uint32_t sectionTimeLeft(int currentPage, int totalPages) const;

 private:
  BookReadingStats book;
  GlobalReadingStats global;
  ReadingStatsSession session;
  std::string directory;
  uint16_t idleSeconds = 300;
  bool active = false;
  bool startUnavailable = false;
  bool dirty = false;
};

static_assert(sizeof(ReaderStats) <= 640, "Review reader statistics RAM growth");
