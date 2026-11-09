#include "ReaderStats.h"

#include <Arduino.h>
#include <Logging.h>

#include <algorithm>

#include "ReadingStatsStore.h"

void ReaderStats::begin(const std::string& cachePath, const std::string& bookPath, bool trackingEnabled,
                        uint16_t requestedIdleSeconds) {
  startUnavailable = false;
  if (!trackingEnabled) return;
  bool bookEnabled = true;
  bool trackingKnown = false;
  book = readingstats::loadBook(cachePath, bookPath, bookEnabled, nullptr, BookReadingStats::LoadMode::Repair,
                                &trackingKnown);
  if (!trackingKnown) {
    startUnavailable = true;
    return;
  }
  if (!bookEnabled) return;
  global = GlobalReadingStats::load();
  active = book.writable && global.writable;
  startUnavailable = !active;
  idleSeconds = std::clamp<uint16_t>(requestedIdleSeconds, 30, 600);
  if (active) directory = readingstats::bookDirectory(cachePath);
}

void ReaderStats::noteTurn(bool forward, bool succeeded) {
  if (active) session.noteTurn(forward, succeeded, millis());
}

void ReaderStats::onRender(ReadingStatsFrame frame) {
  if (!active) return;
  ReadingStatsDateTime now{};
  const bool hasTime = getCurrentLocalReadingStatsDateTime(now);
  session.onPageRendered(millis(), hasTime ? &now : nullptr, frame, book, global, idleSeconds);
  dirty = dirty || session.pendingSeconds() > 0;
}

void ReaderStats::pause() {
  if (active) {
    session.pause(millis(), book, global, idleSeconds);
    dirty = dirty || session.pendingSeconds() > 0;
  }
}

bool ReaderStats::snapshot(BookReadingStats& bookOut, GlobalReadingStats& globalOut, bool& saved) {
  if (!active) return false;
  pause();
  bookOut = book;
  globalOut = global;
  auto preview = session;
  preview.commit(bookOut, globalOut);
  saved = !dirty || (readingstats::saveBook(directory, bookOut) && globalOut.save());
  return true;
}

bool ReaderStats::editBookDate(bool started, const ReadingStatsDate& date, BookReadingStats& bookOut,
                               GlobalReadingStats& globalOut, bool& saved) {
  if (!active || (!date.isValid() && (date.year || date.month || date.day))) return false;
  if (started) {
    book.startDate = date;
    book.startDateManual = true;
  } else {
    book.finishedDate = date;
    book.finishedDateManual = true;
  }
  dirty = true;
  return snapshot(bookOut, globalOut, saved);
}

uint32_t ReaderStats::bookTimeLeft(int progressBasisPoints) const {
  if (!active) return 0;
  auto preview = book;
  // The bar is painted before onRender accrues the preceding turn.
  const uint64_t seconds =
      static_cast<uint64_t>(preview.totalReadingSeconds) + session.previewPendingSeconds(idleSeconds);
  preview.totalReadingSeconds = static_cast<uint32_t>(std::min<uint64_t>(seconds, UINT32_MAX));
  return preview.remainingReadingSeconds(progressBasisPoints);
}

uint32_t ReaderStats::sectionTimeLeft(int currentPage, int totalPages) const {
  if (!active || currentPage < 1 || totalPages <= currentPage) return 0;
  auto preview = book;
  session.previewPendingPace(preview, idleSeconds);
  if (preview.paceSampleCount == 0 || preview.avgSecondsPerForwardPage == 0) return 0;
  const uint64_t seconds = static_cast<uint64_t>(totalPages - currentPage) * preview.avgSecondsPerForwardPage;
  return static_cast<uint32_t>(std::min<uint64_t>(seconds, UINT32_MAX));
}

bool ReaderStats::finish() {
  if (!active) return true;
  pause();
  if (!dirty) return true;
  session.commit(book, global);
  const bool saved = readingstats::saveBook(directory, book) && global.save();
  if (!saved) {
    LOG_ERR("STATS", "Reading statistics save incomplete; previous files retained");
  } else {
    dirty = false;
  }
  return saved;
}
