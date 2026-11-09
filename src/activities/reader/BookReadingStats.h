#pragma once
#include <array>
#include <cstdint>
#include <string>

#include "ReadingStatsUtils.h"

// Per-book reading statistics, persisted to cachePath/stats_v5.bin.
struct BookReadingStats {
  static constexpr size_t CURRENT_FILE_SIZE = 73;
  enum class LoadMode : uint8_t { Repair, ReadOnly };
  // Runtime guard: an unreadable or newer file must never become an empty overwrite.
  bool writable = true;
  uint16_t sessionCount = 0;              // Sessions with at least 60 seconds of tracked reading
  uint32_t totalReadingSeconds = 0;       // Accumulated reading time in seconds
  uint32_t totalPagesTurned = 0;          // Total forward page turns after the dwell threshold
  bool isCompleted = false;               // Whether the user manually marked this book as finished
  uint16_t avgSecondsPerForwardPage = 0;  // Running average pace for time-left estimates
  uint16_t paceSampleCount = 0;           // Number of forward-page pace samples included in the average
  uint32_t estimatedTimeLeftSeconds = 0;  // Preserved legacy estimate; not used for current projections.
  bool startDateManual = false;           // Permanent user override for the reading start date
  bool finishedDateManual = false;        // Permanent user override for the finished date
  ReadingStatsDate startDate;             // First qualifying reading date (or manual override)
  ReadingStatsDate finishedDate;          // Manual or auto-finished date on X3
  std::array<uint32_t, READING_TIME_BUCKET_COUNT> timeOfDaySeconds{};
  std::array<uint32_t, READING_DAY_OF_WEEK_COUNT> dayOfWeekSeconds{};

  // Loads stats from cachePath/stats_v5.bin, with fallback reads from the
  // previous versioned filename and legacy cachePath/stats.bin. ReadOnly
  // suppresses recovery renames; callers must not use its result to save.
  // Returns defaults if none exists; unsupported or damaged data disables writes.
  static BookReadingStats load(const std::string& cachePath, LoadMode mode = LoadMode::Repair);

  // Serialize the same versioned payload used by save().
  bool encode(uint8_t* data, size_t size) const;

  // Saves stats to cachePath/stats_v5.bin through recoverable .tmp/.bak files.
  // Returns false if the new stats file could not be published.
  bool save(const std::string& cachePath) const;

  // Deletes cachePath/stats_v5.bin, its transaction files, the previous
  // versioned filename, and legacy cachePath/stats.bin. Missing files are
  // treated as success.
  static bool remove(const std::string& cachePath);

  // Updates the running reading pace with one forward page dwell sample.
  void recordForwardPageRead(uint32_t seconds);

  // Attributes reading time to local date/time buckets when RTC data exists.
  void recordReadingSpan(const ReadingStatsDateTime& localStart, uint32_t seconds);

  uint32_t remainingReadingSeconds(int progressBasisPoints) const;
  ReadingStatsDate projectedFinishDate(const ReadingStatsDateTime& now, int progressBasisPoints) const;

  // Formats a duration in seconds into a human-readable string.
  // Output examples: "< 1 min", "45 min", "2h 30 min"
  static void formatDuration(uint32_t seconds, char* buf, size_t len);
};
