// Adapted from CrossInk v1.6.1; see docs/downstream/CROSSINK_NOTICE.md.
#include "BookReadingStats.h"

#include <CredentialIntegrity.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_mac.h>

#include <algorithm>
#include <array>
#include <cstring>

#include "StatsAccessGate.h"
#include "StatsSafeReplace.h"

namespace {
// Binary layout v1 (11 bytes):
//   [0]     version (= 1)
//   [1-2]   sessionCount        uint16_t LE
//   [3-6]   totalReadingSeconds uint32_t LE
//   [7-10]  totalPagesTurned    uint32_t LE
//
// Binary layout v2 (12 bytes):
//   [0]     version (= 2)
//   [1-2]   sessionCount        uint16_t LE
//   [3-6]   totalReadingSeconds uint32_t LE
//   [7-10]  totalPagesTurned    uint32_t LE
//   [11]    isCompleted         uint8_t
//
// Binary layout v3 (16 bytes):
//   [0]      version (= 3)
//   [1-2]    sessionCount              uint16_t LE
//   [3-6]    totalReadingSeconds       uint32_t LE
//   [7-10]   totalPagesTurned          uint32_t LE
//   [11]     isCompleted               uint8_t
//   [12-13]  avgSecondsPerForwardPage  uint16_t LE
//   [14-15]  paceSampleCount           uint16_t LE
//
// Binary layout v4 (69 bytes):
//   [0]      version (= 4)
//   [1-2]    sessionCount              uint16_t LE
//   [3-6]    totalReadingSeconds       uint32_t LE
//   [7-10]   totalPagesTurned          uint32_t LE
//   [11]     isCompleted               uint8_t
//   [12-13]  avgSecondsPerForwardPage  uint16_t LE
//   [14-15]  paceSampleCount           uint16_t LE
//   [16]     flags bit0=startDateManual bit1=finishedDateManual
//   [17-18]  startDate.year            uint16_t LE
//   [19]     startDate.month           uint8_t
//   [20]     startDate.day             uint8_t
//   [21-22]  finishedDate.year         uint16_t LE
//   [23]     finishedDate.month        uint8_t
//   [24]     finishedDate.day          uint8_t
//   [25-40]  timeOfDaySeconds[4]       uint32_t LE each
//   [41-68]  dayOfWeekSeconds[7]       uint32_t LE each
//
// Binary layout v5 (73 bytes):
//   [0-68]   v4 fields
//   [69-72]  estimatedTimeLeftSeconds  uint32_t LE, 0 means unavailable
static constexpr uint8_t STATS_FILE_VERSION = BookReadingStats::CURRENT_FILE_VERSION;
static constexpr uint8_t STATS_FILE_VERSION_V2 = 2;
static constexpr uint8_t STATS_FILE_VERSION_V1 = 1;
static constexpr uint8_t STATS_FILE_VERSION_V3 = 3;
static constexpr uint8_t STATS_FILE_VERSION_V4 = 4;
static constexpr int STATS_FILE_SIZE_V1 = 11;
static constexpr int STATS_FILE_SIZE_V2 = 12;
static constexpr int STATS_FILE_SIZE_V3 = 16;
static constexpr int STATS_FILE_SIZE_V4 = 69;
static constexpr int STATS_FILE_SIZE = BookReadingStats::CURRENT_FILE_SIZE;
static constexpr uint16_t MAX_PACE_SAMPLE_COUNT = 1000;
static constexpr uint8_t FLAG_START_DATE_MANUAL = 1u << 0;
static constexpr uint8_t FLAG_FINISHED_DATE_MANUAL = 1u << 1;
static constexpr uint8_t PREVIOUS_VERSIONED_STATS_FILE_VERSION = STATS_FILE_VERSION - 1;
static constexpr const char* LEGACY_STATS_FILE_NAME = "stats.bin";

std::string statsFileNameForVersion(const uint8_t version) {
  char buf[16];
  snprintf(buf, sizeof(buf), "stats_v%u.bin", version);
  return std::string(buf);
}

enum class StatsOpen { Found, Guarded, Missing, Unavailable };

StatsOpen openChecked(const std::string& path, HalFile& file) {
  const auto status = Storage.lookupPath(path.c_str());
  if (status == HalStorage::PathStatus::Failed) return StatsOpen::Unavailable;
  if (status == HalStorage::PathStatus::Missing) return StatsOpen::Missing;
  return Storage.openFileForRead("STATS", path, file) ? StatsOpen::Found : StatsOpen::Unavailable;
}

StatsOpen openRecoverableStatsFile(const std::string& path, HalFile& f, BookReadingStats::LoadMode mode) {
  const auto primary = openChecked(path, f);
  if (primary == StatsOpen::Found) {
    const auto temp = Storage.lookupPath((path + ".tmp").c_str());
    if (temp == HalStorage::PathStatus::Failed) {
      f.close();
      return StatsOpen::Unavailable;
    }
    return temp == HalStorage::PathStatus::Present ? StatsOpen::Guarded : StatsOpen::Found;
  }
  if (primary != StatsOpen::Missing) return primary;

  bool incomplete = false;
  for (const char* suffix : {".tmp", ".bak"}) {
    const std::string recoveryPath = path + suffix;
    const auto recoveryStatus = Storage.lookupPath(recoveryPath.c_str());
    if (recoveryStatus == HalStorage::PathStatus::Failed) return StatsOpen::Unavailable;
    if (recoveryStatus == HalStorage::PathStatus::Missing) continue;

    // A temp file is newer than the backup, but it is safe to prefer only
    // after the complete v5 payload reached storage. A partial temp falls
    // through to the older, known-good backup.
    if (strcmp(suffix, ".tmp") == 0) {
      HalFile candidate;
      if (!Storage.openFileForRead("STATS", recoveryPath, candidate)) return StatsOpen::Unavailable;
      uint8_t data[STATS_FILE_SIZE] = {};
      const size_t fileSize = candidate.fileSize();
      const int n = candidate.read(data, STATS_FILE_SIZE);
      if (!candidate.close()) return StatsOpen::Unavailable;
      if (fileSize > STATS_FILE_SIZE || (n > 0 && data[0] > STATS_FILE_VERSION) ||
          (n < 0 || static_cast<size_t>(n) < fileSize))
        return StatsOpen::Unavailable;
      if (fileSize != STATS_FILE_SIZE || n != STATS_FILE_SIZE || data[0] != STATS_FILE_VERSION) {
        incomplete = true;
        continue;
      }
    }

    if (mode == BookReadingStats::LoadMode::Repair && Storage.rename(recoveryPath.c_str(), path.c_str())) {
      // A reported success is insufficient if both names remain visible.
      if (Storage.lookupPath(recoveryPath.c_str()) == HalStorage::PathStatus::Missing &&
          Storage.lookupPath(path.c_str()) == HalStorage::PathStatus::Present)
        return Storage.openFileForRead("STATS", path, f) ? (incomplete ? StatsOpen::Guarded : StatsOpen::Found)
                                                         : StatsOpen::Unavailable;
    }
    // Failed renames may leave aliased names; never clean them up here.
    return Storage.openFileForRead("STATS", recoveryPath, f) ? StatsOpen::Guarded : StatsOpen::Unavailable;
  }
  return incomplete ? StatsOpen::Unavailable : StatsOpen::Missing;
}

StatsOpen openStatsFileForRead(const std::string& cachePath, HalFile& f, BookReadingStats::LoadMode mode) {
  const std::string currentName = statsFileNameForVersion(STATS_FILE_VERSION);
  const auto current = openRecoverableStatsFile(cachePath + "/" + currentName, f, mode);
  if (current != StatsOpen::Missing) return current;

  // When bumping STATS_FILE_VERSION, this automatically tries the previous
  // versioned filename (e.g. v6 falls back to stats_v5.bin) before the original
  // unversioned stats.bin migration source.
  const std::string previousName = statsFileNameForVersion(PREVIOUS_VERSIONED_STATS_FILE_VERSION);
  const auto previous = openChecked(cachePath + "/" + previousName, f);
  if (previous == StatsOpen::Found) {
    LOG_DBG("STATS", "Migrating %s to %s", previousName.c_str(), currentName.c_str());
    return StatsOpen::Found;
  }
  if (previous == StatsOpen::Unavailable) return previous;

  const auto legacy = openChecked(cachePath + "/" + LEGACY_STATS_FILE_NAME, f);
  if (legacy == StatsOpen::Found) {
    LOG_DBG("STATS", "Migrating legacy %s to %s", LEGACY_STATS_FILE_NAME, currentName.c_str());
  }
  return legacy;
}

uint16_t readLe16(const uint8_t* data, const int offset) {
  return static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
}

uint32_t readLe32(const uint8_t* data, const int offset) {
  return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
         (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
}

void readCommonStats(const uint8_t* data, BookReadingStats& stats) {
  stats.sessionCount = readLe16(data, 1);
  stats.totalReadingSeconds = readLe32(data, 3);
  stats.totalPagesTurned = readLe32(data, 7);
}

void writeLe16(uint8_t* data, const int offset, const uint16_t value) {
  data[offset] = value & 0xFF;
  data[offset + 1] = (value >> 8) & 0xFF;
}

void writeLe32(uint8_t* data, const int offset, const uint32_t value) {
  data[offset] = value & 0xFF;
  data[offset + 1] = (value >> 8) & 0xFF;
  data[offset + 2] = (value >> 16) & 0xFF;
  data[offset + 3] = (value >> 24) & 0xFF;
}

ReadingStatsDate readDate(const uint8_t* data, const int offset) {
  ReadingStatsDate date;
  date.year = readLe16(data, offset);
  date.month = data[offset + 2];
  date.day = data[offset + 3];
  if (!date.isValid()) {
    date.clear();
  }
  return date;
}

}  // namespace

namespace {
bool decodeBookPayload(const uint8_t* data, size_t size, BookReadingStats& stats) {
  if (!data || size == 0) return false;
  if (size == STATS_FILE_SIZE_V1 && data[0] == STATS_FILE_VERSION_V1) {
    readCommonStats(data, stats);
    return true;
  }

  if (size == STATS_FILE_SIZE_V2 && data[0] == STATS_FILE_VERSION_V2) {
    readCommonStats(data, stats);
    stats.isCompleted = data[11] != 0;
    return true;
  }

  if (size == STATS_FILE_SIZE_V3 && data[0] == STATS_FILE_VERSION_V3) {
    readCommonStats(data, stats);
    stats.isCompleted = data[11] != 0;
    stats.avgSecondsPerForwardPage = readLe16(data, 12);
    stats.paceSampleCount = readLe16(data, 14);
    return true;
  }

  if (size != STATS_FILE_SIZE && size != STATS_FILE_SIZE_V4) {
    return false;
  }
  if (size == STATS_FILE_SIZE_V4 && data[0] != STATS_FILE_VERSION_V4) {
    return false;
  }
  if (size == STATS_FILE_SIZE && data[0] != STATS_FILE_VERSION) {
    return false;
  }
  readCommonStats(data, stats);
  stats.isCompleted = data[11] != 0;
  stats.avgSecondsPerForwardPage = readLe16(data, 12);
  stats.paceSampleCount = readLe16(data, 14);
  const uint8_t flags = data[16];
  stats.startDateManual = (flags & FLAG_START_DATE_MANUAL) != 0;
  stats.finishedDateManual = (flags & FLAG_FINISHED_DATE_MANUAL) != 0;
  stats.startDate = readDate(data, 17);
  stats.finishedDate = readDate(data, 21);
  for (size_t i = 0; i < stats.timeOfDaySeconds.size(); ++i) {
    stats.timeOfDaySeconds[i] = readLe32(data, 25 + static_cast<int>(i) * 4);
  }
  for (size_t i = 0; i < stats.dayOfWeekSeconds.size(); ++i) {
    stats.dayOfWeekSeconds[i] = readLe32(data, 41 + static_cast<int>(i) * 4);
  }
  if (size == STATS_FILE_SIZE) {
    stats.estimatedTimeLeftSeconds = readLe32(data, 69);
  }
  return true;
}

BookReadingStats loadBookUnlocked(const std::string& cachePath, BookReadingStats::LoadMode mode) {
  BookReadingStats stats;
  HalFile f;
  const auto opened = openStatsFileForRead(cachePath, f, mode);
  if (opened != StatsOpen::Found && opened != StatsOpen::Guarded) {
    stats.writable = opened == StatsOpen::Missing;
    return stats;
  }
  stats.writable = opened != StatsOpen::Guarded;
  uint8_t data[STATS_FILE_SIZE] = {};
  const size_t size = f.fileSize();
  const int n = f.read(data, STATS_FILE_SIZE);
  const bool closed = f.close();
  if (!closed || n < 0 || size != static_cast<size_t>(n) || size > STATS_FILE_SIZE) {
    stats.writable = false;
    return stats;
  }

  if (!decodeBookPayload(data, size, stats)) {
    stats.writable = false;
    LOG_ERR("STATS", "Unsupported or damaged stats file; preserving on disk");
  }
  return stats;
}

struct CrossInkFormat {
  uint8_t version;
  size_t size;
};
// Tagged RC3 appends a pace fraction in v6 and a counter ID in v7. Native
// statistics retain their v5 format. The original CrossInk bytes stay on card.
constexpr std::array<CrossInkFormat, 4> CROSSINK_FORMATS{{{7, 91}, {6, 75}, {5, 73}, {4, 69}}};
constexpr size_t CROSSINK_SLOT_TRAILER = 8;
static_assert(STATS_FILE_VERSION == 5 && STATS_FILE_SIZE == 73);

enum class CrossInkSlotState { Missing, Unreadable, Invalid, Raw, Checksummed };
struct CrossInkSlot {
  CrossInkSlotState state;
  uint32_t sequence = 0;
  bool valid() const { return state == CrossInkSlotState::Raw || state == CrossInkSlotState::Checksummed; }
};

CrossInkSlot readCrossInkSlot(const std::string& path, CrossInkFormat format, BookReadingStats& stats) {
  HalFile file;
  const auto opened = openChecked(path, file);
  if (opened == StatsOpen::Missing) return {CrossInkSlotState::Missing};
  if (opened != StatsOpen::Found) return {CrossInkSlotState::Unreadable};

  std::array<uint8_t, CROSSINK_FORMATS.front().size + CROSSINK_SLOT_TRAILER> bytes{};
  const size_t size = file.fileSize();
  const bool framed = size == format.size + CROSSINK_SLOT_TRAILER;
  const bool validSize = !file.isDirectory() && (framed || size == format.size);
  const bool ok = !validSize || file.read(bytes.data(), size) == static_cast<int>(size);
  if (!file.close() || !ok) return {CrossInkSlotState::Unreadable};
  if (!validSize || bytes[0] != format.version) return {CrossInkSlotState::Invalid};

  uint32_t sequence = 0;
  if (framed) {
    sequence = readLe32(bytes.data(), format.size);
    const auto checksum =
        credential_integrity::crc32({reinterpret_cast<const char*>(bytes.data()), format.size + sizeof(sequence)});
    if (checksum != readLe32(bytes.data(), format.size + sizeof(sequence))) return {CrossInkSlotState::Invalid};
  }

  uint16_t fraction = 0;
  size_t decodedSize = format.size;
  if (format.version >= 6) {
    fraction = readLe16(bytes.data(), STATS_FILE_SIZE);
    if (fraction >= 1000 || (readLe16(bytes.data(), 12) == UINT16_MAX && fraction != 0))
      return {CrossInkSlotState::Invalid};
    bytes[0] = STATS_FILE_VERSION;
    decodedSize = STATS_FILE_SIZE;
  }
  if (!decodeBookPayload(bytes.data(), decodedSize, stats)) return {CrossInkSlotState::Invalid};
  if (fraction >= 500) ++stats.avgSecondsPerForwardPage;
  return {framed ? CrossInkSlotState::Checksummed : CrossInkSlotState::Raw, sequence};
}

bool readCrossInkFamily(const std::string& path, CrossInkFormat format, BookReadingStats& stats) {
  const auto primary = readCrossInkSlot(path, format, stats);
  BookReadingStats secondary;
  const auto backup = readCrossInkSlot(path + ".bak", format, secondary);
  const bool unreadable =
      primary.state == CrossInkSlotState::Unreadable || backup.state == CrossInkSlotState::Unreadable;
  if (primary.valid() || backup.valid()) {
    const uint32_t distance = backup.sequence - primary.sequence;
    const bool backupNewer = backup.state == CrossInkSlotState::Checksummed &&
                             (primary.state != CrossInkSlotState::Checksummed || (distance && distance < 0x80000000U));
    if (backup.valid() && (!primary.valid() || backupNewer)) stats = secondary;
    stats.writable = !unreadable;
    return true;
  }
  if (!unreadable) {
    const auto temporary = readCrossInkSlot(path + ".tmp", format, stats);
    if (temporary.valid()) return true;
    if (primary.state == CrossInkSlotState::Missing && backup.state == CrossInkSlotState::Missing &&
        temporary.state == CrossInkSlotState::Missing)
      return false;
  }
  stats.writable = false;
  return true;
}

struct CrossInkLoadContext {
  const std::string& path;
  BookReadingStats result;
  bool found = false;
};

bool loadCrossInkLocked(void* input) {
  auto& request = *static_cast<CrossInkLoadContext*>(input);
  const auto directory = Storage.lookupPath(request.path.c_str());
  if (directory == HalStorage::PathStatus::Missing) return true;
  uint8_t mac[6]{};
  if (directory == HalStorage::PathStatus::Failed || !readingstats::normalStatsAllowed() ||
      esp_efuse_mac_get_default(mac) != 0) {
    request.found = true;
    request.result.writable = false;
    return true;
  }
  char identity[22];
  snprintf(identity, sizeof(identity), "crossink-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
           mac[5]);
  for (const auto format : CROSSINK_FORMATS) {
    char filename[40];
    snprintf(filename, sizeof(filename), "/stats_v%u_%s.bin", static_cast<unsigned>(format.version), identity);
    if (readCrossInkFamily(request.path + filename, format, request.result)) {
      request.found = true;
      return true;
    }
  }
  return true;
}

struct LoadContext {
  const std::string& path;
  BookReadingStats::LoadMode mode;
  BookReadingStats result;
};
bool loadBookLocked(void* input) {
  auto& context = *static_cast<LoadContext*>(input);
  if (!readingstats::normalStatsAllowed()) {
    context.result.writable = false;
    return true;
  }
  context.result = loadBookUnlocked(context.path, context.mode);
  return true;
}
}  // namespace

bool BookReadingStats::decode(const uint8_t* data, size_t size, BookReadingStats& output) {
  BookReadingStats candidate;
  if (!decodeBookPayload(data, size, candidate)) return false;
  output = candidate;
  return true;
}

BookReadingStats BookReadingStats::load(const std::string& cachePath, LoadMode mode) {
  LoadContext context{cachePath, mode, {}};
  if (!Storage.withStorageLock(loadBookLocked, &context)) context.result.writable = false;
  return context.result;
}

BookReadingStats BookReadingStats::loadCrossInk(const std::string& cachePath, bool& found) {
  CrossInkLoadContext context{cachePath, {}};
  if (!Storage.withStorageLock(loadCrossInkLocked, &context)) {
    context.found = true;
    context.result.writable = false;
  }
  found = context.found;
  return context.result;
}

void BookReadingStats::recordForwardPageRead(uint32_t seconds) {
  if (seconds == 0) {
    return;
  }
  if (seconds > UINT16_MAX) {
    seconds = UINT16_MAX;
  }

  const uint16_t sample = static_cast<uint16_t>(seconds);
  if (paceSampleCount == 0 || avgSecondsPerForwardPage == 0) {
    avgSecondsPerForwardPage = sample;
    paceSampleCount = 1;
    return;
  }

  const uint16_t weight = paceSampleCount < MAX_PACE_SAMPLE_COUNT ? paceSampleCount : MAX_PACE_SAMPLE_COUNT;
  const uint32_t nextAverage =
      (static_cast<uint32_t>(avgSecondsPerForwardPage) * weight + sample) / (static_cast<uint32_t>(weight) + 1U);
  avgSecondsPerForwardPage = static_cast<uint16_t>(nextAverage);
  if (paceSampleCount < MAX_PACE_SAMPLE_COUNT) {
    paceSampleCount++;
  }
}

void BookReadingStats::recordReadingSpan(const ReadingStatsDateTime& localStart, const uint32_t seconds) {
  recordReadingSpanIntoBuckets(timeOfDaySeconds, dayOfWeekSeconds, localStart, seconds);
}

void BookReadingStats::formatDuration(uint32_t seconds, char* buf, size_t len) {
  if (seconds < 60) {
    snprintf(buf, len, "%s", tr(STR_STATS_LESS_THAN_MIN));
    return;
  }
  const uint32_t hours = seconds / 3600;
  const uint32_t minutes = (seconds % 3600) / 60;
  if (hours == 0) {
    snprintf(buf, len, tr(STR_STATS_MINUTES_FORMAT), static_cast<unsigned long>(minutes));
  } else {
    snprintf(buf, len, tr(STR_STATS_HOURS_MINUTES_FORMAT), static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes));
  }
}

bool BookReadingStats::encode(uint8_t* data, size_t size) const {
  if (!writable || !data || size != CURRENT_FILE_SIZE) return false;
  memset(data, 0, size);
  data[0] = STATS_FILE_VERSION;
  writeLe16(data, 1, sessionCount);
  writeLe32(data, 3, totalReadingSeconds);
  writeLe32(data, 7, totalPagesTurned);
  data[11] = isCompleted ? 1 : 0;
  writeLe16(data, 12, avgSecondsPerForwardPage);
  writeLe16(data, 14, paceSampleCount);
  data[16] = (startDateManual ? FLAG_START_DATE_MANUAL : 0u) | (finishedDateManual ? FLAG_FINISHED_DATE_MANUAL : 0u);
  writeLe16(data, 17, startDate.isValid() ? startDate.year : 0);
  data[19] = startDate.isValid() ? startDate.month : 0;
  data[20] = startDate.isValid() ? startDate.day : 0;
  writeLe16(data, 21, finishedDate.isValid() ? finishedDate.year : 0);
  data[23] = finishedDate.isValid() ? finishedDate.month : 0;
  data[24] = finishedDate.isValid() ? finishedDate.day : 0;
  for (size_t i = 0; i < timeOfDaySeconds.size(); ++i) {
    writeLe32(data, 25 + static_cast<int>(i) * 4, timeOfDaySeconds[i]);
  }
  for (size_t i = 0; i < dayOfWeekSeconds.size(); ++i) {
    writeLe32(data, 41 + static_cast<int>(i) * 4, dayOfWeekSeconds[i]);
  }
  writeLe32(data, 69, estimatedTimeLeftSeconds);

  return true;
}

namespace {
struct BookSaveContext {
  const std::string& cachePath;
  const uint8_t* data;
};
bool saveBookLocked(void* raw) {
  const auto& context = *static_cast<BookSaveContext*>(raw);
  return readingstats::normalStatsAllowed() &&
         statsreplace::save("STATS", context.cachePath + "/" + statsFileNameForVersion(STATS_FILE_VERSION),
                            context.data, STATS_FILE_SIZE, false);
}
}  // namespace

bool BookReadingStats::save(const std::string& cachePath) const {
  if (!writable) return false;
  uint8_t data[STATS_FILE_SIZE];
  if (!encode(data, sizeof(data))) return false;
  BookSaveContext context{cachePath, data};
  return Storage.withStorageLock(saveBookLocked, &context);
}

namespace {
struct RemoveContext {
  const std::string& path;
};
bool removeBookLocked(void* input) {
  if (!readingstats::normalStatsAllowed()) return false;
  const auto& cachePath = static_cast<RemoveContext*>(input)->path;
  const std::string statsFileName = statsFileNameForVersion(STATS_FILE_VERSION);
  const std::string statsPath = cachePath + "/" + statsFileName;
  const std::string previousStatsFileName = statsFileNameForVersion(PREVIOUS_VERSIONED_STATS_FILE_VERSION);
  const std::string previousStatsPath = cachePath + "/" + previousStatsFileName;
  const std::string legacyStatsPath = cachePath + "/" + LEGACY_STATS_FILE_NAME;
  // A temp may share storage with another entry. Keep everything for review.
  if (Storage.lookupPath((statsPath + ".tmp").c_str()) != HalStorage::PathStatus::Missing) return false;
  const std::array<std::string, 4> paths{legacyStatsPath, previousStatsPath, statsPath + ".bak", statsPath};
  std::array<bool, 4> present{};
  for (size_t i = 0; i < paths.size(); ++i) {
    const auto status = Storage.lookupPath(paths[i].c_str());
    if (status == HalStorage::PathStatus::Failed) return false;
    present[i] = status == HalStorage::PathStatus::Present;
  }
  for (size_t i = 0; i < paths.size(); ++i)
    for (size_t j = i + 1; j < paths.size(); ++j)
      if (present[i] && present[j] && !Storage.distinctFirstClusters(paths[i].c_str(), paths[j].c_str())) return false;
  // Remove older fallback copies first and keep the canonical file until all
  // checked removals succeed, preventing an older version from resurfacing.
  for (size_t i = 0; i < paths.size(); ++i) {
    if (present[i] && !Storage.remove(paths[i].c_str())) {
      LOG_ERR("STATS", "Could not remove statistics file: %s", paths[i].c_str());
      return false;
    }
  }
  return true;
}
}  // namespace

bool BookReadingStats::remove(const std::string& cachePath) {
  RemoveContext context{cachePath};
  return Storage.withStorageLock(removeBookLocked, &context);
}

uint32_t BookReadingStats::remainingReadingSeconds(int progressBasisPoints) const {
  if (!writable || isCompleted) return 0;
  if (totalReadingSeconds < 120 || progressBasisPoints <= 0 || progressBasisPoints >= 10000) return 0;
  const uint64_t numerator = static_cast<uint64_t>(totalReadingSeconds) * (10000 - progressBasisPoints);
  return static_cast<uint32_t>(
      std::min<uint64_t>((numerator + progressBasisPoints / 2) / progressBasisPoints, UINT32_MAX));
}

ReadingStatsDate BookReadingStats::projectedFinishDate(const ReadingStatsDateTime& now, int progressBasisPoints) const {
  const uint32_t remaining = remainingReadingSeconds(progressBasisPoints);
  if (!now.isValid() || !remaining) return {};
  uint32_t calendarSeconds = remaining;
  if (startDate.isValid() && totalReadingSeconds) {
    const uint16_t days = std::max<uint16_t>(1, readingSpanDaysElapsed(startDate, now.date));
    const double estimate = static_cast<double>(remaining) * days * 86400 / totalReadingSeconds;
    if (estimate >= 0.5) {
      calendarSeconds = static_cast<uint32_t>(std::min(estimate + 0.5, static_cast<double>(UINT32_MAX)));
    }
  }
  const uint64_t secondsToday = now.hour * 3600U + now.minute * 60U + now.second;
  const uint32_t day = readingStatsDayIndex(now.date) + static_cast<uint32_t>((secondsToday + calendarSeconds) / 86400);
  ReadingStatsDate result;
  readingStatsDateFromDayIndex(day, result);
  return result;
}
