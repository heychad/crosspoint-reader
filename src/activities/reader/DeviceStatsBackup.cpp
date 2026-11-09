#include "DeviceStatsBackup.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Memory.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include "GlobalReadingStats.h"

namespace {
constexpr char DIRECTORY[] = "/.crossink-stats-backup";
constexpr char DEVICE_STATS[] = "/.crosspoint/global_stats.bin";

uint64_t hashBytes(const std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE>& bytes) {
  uint64_t hash = 14695981039346656037ULL;
  for (const auto byte : bytes) hash = (hash ^ byte) * 1099511628211ULL;
  return hash;
}

bool matchesPrimary(const GlobalReadingStats& expected) {
  const auto current = GlobalReadingStats::loadReadOnly();
  return current.writable && expected.writable && current.totalSessions == expected.totalSessions &&
         current.totalReadingSeconds == expected.totalReadingSeconds &&
         current.totalPagesTurned == expected.totalPagesTurned && current.completedBooks == expected.completedBooks &&
         current.timeOfDaySeconds == expected.timeOfDaySeconds &&
         current.dayOfWeekSeconds == expected.dayOfWeekSeconds &&
         current.readingHistoryAnchorDay == expected.readingHistoryAnchorDay &&
         current.readingHistoryBits == expected.readingHistoryBits &&
         current.longestReadingStreak == expected.longestReadingStreak;
}

bool verifiedFile(const char* path, const std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE>& expected) {
  HalFile file;
  if (!Storage.openFileForRead("STATS", path, file)) return false;
  bool matches = file.fileSize() == expected.size();
  uint8_t chunk[64];
  for (size_t offset = 0; matches && offset < expected.size();) {
    const size_t count = std::min(sizeof(chunk), expected.size() - offset);
    matches = file.read(chunk, count) == static_cast<int>(count) && memcmp(chunk, expected.data() + offset, count) == 0;
    offset += count;
  }
  return file.close() && matches;
}
}  // namespace

bool statsbackup::createDeviceBackup(const GlobalReadingStats& stats) {
  if (Storage.lookupPath(DEVICE_STATS) != HalStorage::PathStatus::Present || !matchesPrimary(stats)) return false;
  auto bytes = makeUniqueNoThrow<std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE>>();
  if (!bytes || !stats.encode(*bytes)) return false;
  const auto dir = Storage.lookupPath(DIRECTORY);
  if (dir == HalStorage::PathStatus::Failed || (dir == HalStorage::PathStatus::Missing && !Storage.mkdir(DIRECTORY)))
    return false;
  const auto hash = hashBytes(*bytes);
  const uint32_t sequence = millis() + 1;
  if (!sequence) return false;
  char filename[64];
  snprintf(filename, sizeof(filename), "stats_cp_%010lu_M_00000000_000000_%016llx.bin",
           static_cast<unsigned long>(sequence), static_cast<unsigned long long>(hash));
  const std::string target = std::string(DIRECTORY) + "/" + filename;
  const std::string previousPartial = target + ".part";
  if (Storage.lookupPath(target.c_str()) != HalStorage::PathStatus::Missing ||
      Storage.lookupPath(previousPartial.c_str()) != HalStorage::PathStatus::Missing)
    return false;
  // A failed rename can leave two directory entries sharing one cluster.
  // Write a new, exclusive target instead and preserve any incomplete file.
  HalFile file = Storage.open(target.c_str(), O_WRONLY | O_CREAT | O_EXCL);
  if (!file) return false;
  const bool written = file.write(bytes->data(), bytes->size()) == bytes->size();
  file.flush();
  if (!file.close() || !written) return false;
  return verifiedFile(target.c_str(), *bytes);
}
