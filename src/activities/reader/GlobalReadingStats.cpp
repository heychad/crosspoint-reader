// Adapted from CrossInk v1.6.1; see docs/downstream/CROSSINK_NOTICE.md.
#include "GlobalReadingStats.h"

#include <HalStorage.h>
#include <Logging.h>
#include <esp_mac.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

#include "StatsAccessGate.h"
#include "StatsSafeReplace.h"

namespace {
enum class StatsLoadResult : uint8_t { Ok, Invalid, NewerFormat, Unreadable };

struct StatsLoadOutcome {
  StatsLoadResult result = StatsLoadResult::Invalid;
  uint8_t version = 0;
  size_t fileSize = 0;
};

// Binary layout v1 (13 bytes):
//   [0]     version (= 1)
//   [1-4]   totalSessions       uint32_t LE
//   [5-8]   totalReadingSeconds uint32_t LE
//   [9-12]  totalPagesTurned    uint32_t LE
//
// Binary layout v2 (17 bytes):
//   [0]      version (= 2)
//   [1-4]    totalSessions       uint32_t LE
//   [5-8]    totalReadingSeconds uint32_t LE
//   [9-12]   totalPagesTurned    uint32_t LE
//   [13-16]  completedBooks      uint32_t LE
//
// Binary layout v3 (159 bytes):
//   [0]       version (= 3)
//   [1-4]     totalSessions             uint32_t LE
//   [5-8]     totalReadingSeconds       uint32_t LE
//   [9-12]    totalPagesTurned          uint32_t LE
//   [13-16]   completedBooks            uint32_t LE
//   [17-32]   timeOfDaySeconds[4]       uint32_t LE each
//   [33-60]   dayOfWeekSeconds[7]       uint32_t LE each
//   [61-64]   readingHistoryAnchorDay   uint32_t LE
//   [65-156]  readingHistoryBits[92]    uint8_t
//   [157-158] longestReadingStreak      uint16_t LE
static constexpr uint8_t GLOBAL_STATS_VERSION = GlobalReadingStats::CURRENT_FILE_VERSION;
static constexpr uint8_t GLOBAL_STATS_VERSION_V1 = 1;
static constexpr int GLOBAL_STATS_FILE_SIZE_V1 = 13;
static constexpr uint8_t GLOBAL_STATS_VERSION_V2 = 2;
static constexpr int GLOBAL_STATS_FILE_SIZE_V2 = 17;
static constexpr int GLOBAL_STATS_FILE_SIZE = static_cast<int>(GlobalReadingStats::CURRENT_FILE_SIZE);
static constexpr char GLOBAL_STATS_PATH[] = "/.crosspoint/global_stats.bin";

static constexpr char GLOBAL_STATS_BAK_PATH[] = "/.crosspoint/global_stats.bin.bak";

static constexpr char SYNCED_STATS_DIR[] = "/.crosspoint/synced_stats";

uint32_t addSaturated(uint32_t a, uint32_t b) {
  return std::numeric_limits<uint32_t>::max() - a < b ? std::numeric_limits<uint32_t>::max() : a + b;
}

void addStats(GlobalReadingStats& target, const GlobalReadingStats& source) {
  target.totalSessions = addSaturated(target.totalSessions, source.totalSessions);
  target.totalReadingSeconds = addSaturated(target.totalReadingSeconds, source.totalReadingSeconds);
  target.totalPagesTurned = addSaturated(target.totalPagesTurned, source.totalPagesTurned);
  target.completedBooks = addSaturated(target.completedBooks, source.completedBooks);
  for (size_t i = 0; i < target.timeOfDaySeconds.size(); ++i)
    target.timeOfDaySeconds[i] = addSaturated(target.timeOfDaySeconds[i], source.timeOfDaySeconds[i]);
  for (size_t i = 0; i < target.dayOfWeekSeconds.size(); ++i)
    target.dayOfWeekSeconds[i] = addSaturated(target.dayOfWeekSeconds[i], source.dayOfWeekSeconds[i]);
  mergeReadingHistory(target.readingHistoryAnchorDay, target.readingHistoryBits, source.readingHistoryAnchorDay,
                      source.readingHistoryBits);
  target.longestReadingStreak = std::max(target.longestReadingStreak, source.longestReadingStreak);
}

bool normalizeDeviceFile(char* name, size_t length) {
  if (length != 23 || strncmp(name, "device_", 7) != 0 || strcmp(name + 19, ".bin") != 0) return false;
  for (size_t i = 7; i < 19; ++i) {
    if (name[i] >= 'A' && name[i] <= 'F') name[i] += 'a' - 'A';
    if ((name[i] < '0' || name[i] > '9') && (name[i] < 'a' || name[i] > 'f')) return false;
  }
  return true;
}

uint32_t readLe32(const uint8_t* data, const int offset) {
  return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
         (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
}

void loadCommonFields(const uint8_t* data, GlobalReadingStats& out) {
  out.totalSessions = readLe32(data, 1);
  out.totalReadingSeconds = readLe32(data, 5);
  out.totalPagesTurned = readLe32(data, 9);
}

uint16_t readLe16(const uint8_t* data, const int offset) {
  return static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
}

void serializeStats(const GlobalReadingStats& stats, uint8_t* data) {
  data[0] = GLOBAL_STATS_VERSION;
  data[1] = stats.totalSessions & 0xFF;
  data[2] = (stats.totalSessions >> 8) & 0xFF;
  data[3] = (stats.totalSessions >> 16) & 0xFF;
  data[4] = (stats.totalSessions >> 24) & 0xFF;
  data[5] = stats.totalReadingSeconds & 0xFF;
  data[6] = (stats.totalReadingSeconds >> 8) & 0xFF;
  data[7] = (stats.totalReadingSeconds >> 16) & 0xFF;
  data[8] = (stats.totalReadingSeconds >> 24) & 0xFF;
  data[9] = stats.totalPagesTurned & 0xFF;
  data[10] = (stats.totalPagesTurned >> 8) & 0xFF;
  data[11] = (stats.totalPagesTurned >> 16) & 0xFF;
  data[12] = (stats.totalPagesTurned >> 24) & 0xFF;
  data[13] = stats.completedBooks & 0xFF;
  data[14] = (stats.completedBooks >> 8) & 0xFF;
  data[15] = (stats.completedBooks >> 16) & 0xFF;
  data[16] = (stats.completedBooks >> 24) & 0xFF;
  for (size_t i = 0; i < stats.timeOfDaySeconds.size(); ++i) {
    const uint32_t value = stats.timeOfDaySeconds[i];
    const int offset = 17 + static_cast<int>(i) * 4;
    data[offset] = value & 0xFF;
    data[offset + 1] = (value >> 8) & 0xFF;
    data[offset + 2] = (value >> 16) & 0xFF;
    data[offset + 3] = (value >> 24) & 0xFF;
  }
  for (size_t i = 0; i < stats.dayOfWeekSeconds.size(); ++i) {
    const uint32_t value = stats.dayOfWeekSeconds[i];
    const int offset = 33 + static_cast<int>(i) * 4;
    data[offset] = value & 0xFF;
    data[offset + 1] = (value >> 8) & 0xFF;
    data[offset + 2] = (value >> 16) & 0xFF;
    data[offset + 3] = (value >> 24) & 0xFF;
  }
  data[61] = stats.readingHistoryAnchorDay & 0xFF;
  data[62] = (stats.readingHistoryAnchorDay >> 8) & 0xFF;
  data[63] = (stats.readingHistoryAnchorDay >> 16) & 0xFF;
  data[64] = (stats.readingHistoryAnchorDay >> 24) & 0xFF;
  memcpy(data + 65, stats.readingHistoryBits.data(), stats.readingHistoryBits.size());
  data[157] = stats.longestReadingStreak & 0xFF;
  data[158] = (stats.longestReadingStreak >> 8) & 0xFF;
}

StatsLoadOutcome decodeStats(const uint8_t* data, size_t size, GlobalReadingStats& out) {
  StatsLoadOutcome outcome;
  outcome.fileSize = size;
  if (!data || size == 0) return outcome;
  outcome.version = data[0];

  if (outcome.fileSize > static_cast<size_t>(GLOBAL_STATS_FILE_SIZE) || outcome.version > GLOBAL_STATS_VERSION) {
    outcome.result = StatsLoadResult::NewerFormat;
    return outcome;
  }

  if (size == GLOBAL_STATS_FILE_SIZE_V1 && data[0] == GLOBAL_STATS_VERSION_V1) {
    loadCommonFields(data, out);
    out.completedBooks = 0;
    outcome.result = StatsLoadResult::Ok;
    return outcome;
  }

  if (size == GLOBAL_STATS_FILE_SIZE_V2 && data[0] == GLOBAL_STATS_VERSION_V2) {
    loadCommonFields(data, out);
    out.completedBooks = readLe32(data, 13);
    outcome.result = StatsLoadResult::Ok;
    return outcome;
  }

  if (size != GLOBAL_STATS_FILE_SIZE || data[0] != GLOBAL_STATS_VERSION) return outcome;
  loadCommonFields(data, out);
  out.completedBooks = readLe32(data, 13);
  for (size_t i = 0; i < out.timeOfDaySeconds.size(); ++i) {
    out.timeOfDaySeconds[i] = readLe32(data, 17 + static_cast<int>(i) * 4);
  }
  for (size_t i = 0; i < out.dayOfWeekSeconds.size(); ++i) {
    out.dayOfWeekSeconds[i] = readLe32(data, 33 + static_cast<int>(i) * 4);
  }
  out.readingHistoryAnchorDay = readLe32(data, 61);
  memcpy(out.readingHistoryBits.data(), data + 65, out.readingHistoryBits.size());
  out.longestReadingStreak = readLe16(data, 157);
  outcome.result = StatsLoadResult::Ok;
  return outcome;
}

StatsLoadOutcome loadFromOpenFile(HalFile& f, GlobalReadingStats& out) {
  const size_t size = f.fileSize();
  uint8_t data[GLOBAL_STATS_FILE_SIZE] = {};
  const size_t bytesToRead = std::min(size, static_cast<size_t>(GLOBAL_STATS_FILE_SIZE));
  const int n = f.read(data, bytesToRead);
  if (n < 0 || static_cast<size_t>(n) != bytesToRead) {
    StatsLoadOutcome outcome;
    outcome.fileSize = size;
    outcome.result = StatsLoadResult::Unreadable;
    return outcome;
  }
  return decodeStats(data, size, out);
}

bool saveToFile(const GlobalReadingStats& stats, const char* path, const char* backupPath) {
  if (!backupPath || std::string(backupPath) != std::string(path) + ".bak") return false;
  uint8_t data[GLOBAL_STATS_FILE_SIZE];
  serializeStats(stats, data);
  return statsreplace::save("GSTATS", path, data, sizeof(data), true);
}

}  // namespace

static StatsLoadOutcome loadFromFile(const char* path, GlobalReadingStats& out) {
  StatsLoadOutcome outcome;
  HalFile f;
  const auto status = Storage.lookupPath(path);
  if (status == HalStorage::PathStatus::Failed) {
    outcome.result = StatsLoadResult::Unreadable;
    return outcome;
  }
  if (status == HalStorage::PathStatus::Missing) return outcome;
  if (!Storage.openFileForRead("GSTATS", path, f)) {
    outcome.result = StatsLoadResult::Unreadable;
    return outcome;
  }
  outcome = loadFromOpenFile(f, out);
  if (!f.close()) outcome.result = StatsLoadResult::Unreadable;
  return outcome;
}

bool GlobalReadingStats::decode(const uint8_t* data, size_t size, GlobalReadingStats& output) {
  GlobalReadingStats candidate;
  if (decodeStats(data, size, candidate).result != StatsLoadResult::Ok) return false;
  output = candidate;
  return true;
}

bool GlobalReadingStats::encode(std::array<uint8_t, CURRENT_FILE_SIZE>& output) const {
  if (!writable) return false;
  serializeStats(*this, output.data());
  return true;
}

GlobalReadingStats GlobalReadingStats::loadReadOnly() {
  GlobalReadingStats stats;
  const auto load = [](void* raw) {
    auto& resultStats = *static_cast<GlobalReadingStats*>(raw);
    if (!readingstats::normalStatsAllowed()) {
      resultStats.writable = false;
      return true;
    }
    HalFile file;
    if (!Storage.openFileForRead("GSTATS", GLOBAL_STATS_PATH, file)) {
      resultStats.writable = false;
      return true;
    }
    const auto result = loadFromOpenFile(file, resultStats);
    if (!file.close() || result.result != StatsLoadResult::Ok ||
        Storage.lookupPath("/.crosspoint/global_stats.bin.tmp") != HalStorage::PathStatus::Missing)
      resultStats.writable = false;
    return true;
  };
  if (!Storage.withStorageLock(load, &stats)) stats.writable = false;
  return stats;
}

namespace {
GlobalReadingStats loadGlobalUnlocked() {
  GlobalReadingStats stats;
  constexpr char tempPath[] = "/.crosspoint/global_stats.bin.tmp";
  const StatsLoadOutcome primary = loadFromFile(GLOBAL_STATS_PATH, stats);
  if (primary.result == StatsLoadResult::Ok) {
    if (Storage.lookupPath(tempPath) != HalStorage::PathStatus::Missing) stats.writable = false;
    return stats;
  }
  if (primary.result == StatsLoadResult::Unreadable) {
    stats.writable = false;
    return stats;
  }
  if (primary.result == StatsLoadResult::NewerFormat) {
    stats.writable = false;
    LOG_ERR("GSTATS", "On-disk stats are from a newer build (v%u, %u bytes); refusing to overwrite", primary.version,
            static_cast<unsigned>(primary.fileSize));
    return stats;
  }

  const StatsLoadOutcome temp = loadFromFile(tempPath, stats);
  if (temp.result == StatsLoadResult::Ok && temp.version == GLOBAL_STATS_VERSION &&
      temp.fileSize == GLOBAL_STATS_FILE_SIZE) {
    // A failed promotion can leave two names sharing storage; never delete on
    // failure and permit writes only after the source name is confirmed gone.
    stats.writable = Storage.lookupPath(GLOBAL_STATS_PATH) == HalStorage::PathStatus::Missing &&
                     Storage.rename(tempPath, GLOBAL_STATS_PATH) &&
                     Storage.lookupPath(tempPath) == HalStorage::PathStatus::Missing &&
                     Storage.lookupPath(GLOBAL_STATS_PATH) == HalStorage::PathStatus::Present;
    return stats;
  }
  if (temp.result == StatsLoadResult::NewerFormat || temp.result == StatsLoadResult::Unreadable) {
    stats.writable = false;
    return stats;
  }

  const StatsLoadOutcome backup = loadFromFile(GLOBAL_STATS_BAK_PATH, stats);
  if (backup.result == StatsLoadResult::Unreadable) {
    stats.writable = false;
    return stats;
  }
  if (backup.result == StatsLoadResult::Ok) {
    LOG_DBG("GSTATS", "Recovered global stats from backup");
    // The save rotation could otherwise delete the only valid copy.
    stats.writable = false;
    return stats;
  }
  if (backup.result == StatsLoadResult::NewerFormat) {
    stats.writable = false;
    LOG_ERR("GSTATS", "Backup stats are from a newer build (v%u, %u bytes); refusing to overwrite", backup.version,
            static_cast<unsigned>(backup.fileSize));
    return stats;
  }

  stats.writable = Storage.lookupPath(GLOBAL_STATS_PATH) == HalStorage::PathStatus::Missing &&
                   Storage.lookupPath(GLOBAL_STATS_BAK_PATH) == HalStorage::PathStatus::Missing &&
                   Storage.lookupPath(tempPath) == HalStorage::PathStatus::Missing;
  LOG_DBG("GSTATS", "Global stats unavailable; existing unreadable files are preserved");
  return stats;
}

bool loadGlobalLocked(void* input) {
  auto& stats = *static_cast<GlobalReadingStats*>(input);
  if (!readingstats::normalStatsAllowed()) {
    stats.writable = false;
    return true;
  }
  stats = loadGlobalUnlocked();
  return true;
}
}  // namespace

GlobalReadingStats GlobalReadingStats::load() {
  GlobalReadingStats stats;
  if (!Storage.withStorageLock(loadGlobalLocked, &stats)) stats.writable = false;
  return stats;
}

GlobalStatsAggregate GlobalReadingStats::loadAggregated(const GlobalReadingStats& local) {
  GlobalStatsAggregate result{local, local.writable ? 1U : 0U, 0, local.writable};
  if (!local.writable) return result;
  auto directory = Storage.open(SYNCED_STATS_DIR);
  if (!directory || !directory.isDirectory()) {
    result.complete = !Storage.exists(SYNCED_STATS_DIR);
    if (!result.complete) LOG_ERR("GSTATS", "Cannot read synchronized statistics directory");
    return result;
  }
  uint8_t mac[6]{};
  if (esp_efuse_mac_get_default(mac) != 0) {
    LOG_ERR("GSTATS", "Cannot identify local statistics source");
    result.complete = false;
    return result;
  }
  char localName[32];
  snprintf(localName, sizeof(localName), "device_%02x%02x%02x%02x%02x%02x.bin", mac[0], mac[1], mac[2], mac[3], mac[4],
           mac[5]);
  for (auto file = directory.openNextFile(); file; file = directory.openNextFile()) {
    char name[32];
    const size_t length = file.getName(name, sizeof(name));
    if (file.isDirectory() || !normalizeDeviceFile(name, length) || strcmp(name, localName) == 0) {
      file.close();
      continue;
    }
    GlobalReadingStats peer;
    const auto outcome = loadFromOpenFile(file, peer);
    const bool closed = file.close();
    if (closed && outcome.result == StatsLoadResult::Ok) {
      addStats(result.stats, peer);
      result.sources = addSaturated(result.sources, 1);
    } else {
      result.skipped = addSaturated(result.skipped, 1);
      result.complete = false;
      LOG_ERR("GSTATS", "Cannot include synchronized statistics source");
    }
  }
  if (!directory.close()) {
    LOG_ERR("GSTATS", "Could not close synchronized statistics directory");
    result.complete = false;
  }
  return result;
}

bool GlobalReadingStats::save() const {
  if (!writable) {
    LOG_ERR("GSTATS", "Refusing to overwrite on-disk stats after newer-format file was detected");
    return false;
  }
  const auto save = [](void* raw) {
    const auto& stats = **static_cast<const GlobalReadingStats**>(raw);
    return readingstats::normalStatsAllowed() && saveToFile(stats, GLOBAL_STATS_PATH, GLOBAL_STATS_BAK_PATH);
  };
  const GlobalReadingStats* stats = this;
  return Storage.withStorageLock(save, &stats);
}

void GlobalReadingStats::recordReadingSpan(const ReadingStatsDateTime& localStart, const uint32_t seconds) {
  recordReadingSpanIntoBuckets(timeOfDaySeconds, dayOfWeekSeconds, localStart, seconds);
  recordReadingSpanIntoHistory(readingHistoryAnchorDay, readingHistoryBits, localStart, seconds);

  const uint16_t historyLongest = computeReadingHistoryLongestStreak(readingHistoryAnchorDay, readingHistoryBits);
  if (historyLongest > longestReadingStreak) {
    longestReadingStreak = historyLongest;
  }
}

uint16_t GlobalReadingStats::currentReadingStreak(const ReadingStatsDate* today) const {
  return computeReadingHistoryCurrentStreak(readingHistoryAnchorDay, readingHistoryBits, today);
}

uint16_t GlobalReadingStats::displayLongestReadingStreak() const {
  return std::max(longestReadingStreak,
                  computeReadingHistoryLongestStreak(readingHistoryAnchorDay, readingHistoryBits));
}
