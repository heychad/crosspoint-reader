#pragma once
#include <array>
#include <cstdint>

#include "ReadingStatsUtils.h"

struct GlobalStatsAggregate;

// Cumulative reading statistics across all books, persisted to
// /.crosspoint/global_stats.bin.
struct GlobalReadingStats {
  bool writable = true;              // Runtime guard; never serialized.
  uint32_t totalSessions = 0;        // Qualifying sessions across all books
  uint32_t totalReadingSeconds = 0;  // Accumulated reading time across all books
  uint32_t totalPagesTurned = 0;     // Total forward page turns after the dwell threshold
  uint32_t completedBooks = 0;       // Books manually marked as finished
  std::array<uint32_t, READING_TIME_BUCKET_COUNT> timeOfDaySeconds{};
  std::array<uint32_t, READING_DAY_OF_WEEK_COUNT> dayOfWeekSeconds{};
  uint32_t readingHistoryAnchorDay = 0;
  std::array<uint8_t, READING_HISTORY_BYTES> readingHistoryBits{};
  uint16_t longestReadingStreak = 0;

  static constexpr uint8_t CURRENT_FILE_VERSION = 3;
  static constexpr size_t CURRENT_FILE_SIZE = 159;
  static constexpr size_t MIN_SUPPORTED_FILE_SIZE = 13;
  static constexpr size_t CROSSINK_V4_FILE_SIZE = 175;

  // Loads native stats, or imports the current reader's CrossInk stats on first use.
  // Incompatible or unreadable files disable writes.
  static GlobalReadingStats load();
  // Read only the primary snapshot; never promote a recovery file.
  static GlobalReadingStats loadReadOnly();

  static GlobalStatsAggregate loadAggregated(const GlobalReadingStats& local);

  // Decode a supported complete payload. Failure leaves output unchanged.
  static bool decode(const uint8_t* data, size_t size, GlobalReadingStats& output);

  // Translate an external snapshot in a caller-owned buffer before decode().
  // The RC2/RC3 counter identity is outside the native totals and history.
  static size_t normalizeCrossInkSnapshot(uint8_t* data, size_t size) {
    static_assert(CURRENT_FILE_VERSION == 3 && CURRENT_FILE_SIZE == 159);
    if (size != CROSSINK_V4_FILE_SIZE || data[0] != 4) return size;
    data[0] = CURRENT_FILE_VERSION;
    return CURRENT_FILE_SIZE;
  }

  // Encode the current supported format without writing to storage.
  bool encode(std::array<uint8_t, CURRENT_FILE_SIZE>& output) const;

  // Saves stats to /.crosspoint/global_stats.bin.
  bool save() const;

  void recordReadingSpan(const ReadingStatsDateTime& localStart, uint32_t seconds);
  uint16_t currentReadingStreak(const ReadingStatsDate* today) const;
  uint16_t displayLongestReadingStreak() const;
};

struct GlobalStatsAggregate {
  GlobalReadingStats stats;
  uint32_t sources = 0;
  uint32_t skipped = 0;
  bool complete = false;
};
