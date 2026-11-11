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
#include "StatsAccessGate.h"
#include "StatsSessionLease.h"

namespace {
constexpr char DIRECTORY[] = "/.crossink-stats-backup";
constexpr char DEVICE_STATS[] = "/.crosspoint/global_stats.bin";
constexpr char DEVICE_STATS_BACKUP[] = "/.crosspoint/global_stats.bin.bak";

uint64_t hashBytes(const std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE>& bytes) {
  uint64_t hash = 14695981039346656037ULL;
  for (const auto byte : bytes) hash = (hash ^ byte) * 1099511628211ULL;
  return hash;
}

bool backupFilenameHash(const std::string& name, uint64_t& hash) {
  if (name.compare(0, 9, "stats_cp_") != 0) return false;
  const bool manual =
      name.size() == 58 && name.compare(19, 19, "_M_00000000_000000_") == 0 && name.compare(54, 4, ".bin") == 0;
  const bool automatic =
      name.size() == 40 && name.compare(36, 4, ".bin") == 0 &&
      (name.compare(17, 3, "_A_") == 0 || (name.compare(17, 3, "_U_") == 0 && name.compare(9, 8, "00000000") == 0));
  if (!manual && !automatic) return false;
  for (size_t i = 9; i < (manual ? 19 : 17); ++i)
    if (name[i] < '0' || name[i] > '9') return false;
  uint64_t parsed = 0;
  const size_t offset = manual ? 38 : 20;
  for (size_t i = offset; i < offset + 16; ++i) {
    const char ch = name[i];
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    parsed = (parsed << 4) | static_cast<uint64_t>(ch <= '9' ? ch - '0' : ch - 'a' + 10);
  }
  hash = parsed;
  return true;
}

bool legacyBackupFilename(const std::string& name) {
  const auto digit = [&name](size_t i) { return name[i] >= '0' && name[i] <= '9'; };
  if (name.starts_with("stats_backup_") && name.ends_with(".bin")) {
    if (name.size() <= 17 || name.size() > 27) return false;
    bool nonzero = false;
    for (size_t i = 13; i < name.size() - 4; ++i) {
      if (!digit(i)) return false;
      nonzero |= name[i] != '0';
    }
    return nonzero;
  }
  if ((name.size() != 20 && name.size() != 25) || !name.starts_with("stats_") || !name.ends_with(".bin") ||
      name[10] != '-' || name[13] != '-')
    return false;
  for (size_t i : std::array<size_t, 8>{6, 7, 8, 9, 11, 12, 14, 15})
    if (!digit(i)) return false;
  if (name.size() == 25) {
    if (name[16] != '_') return false;
    for (size_t i = 17; i < 21; ++i)
      if (!digit(i)) return false;
  }
  return true;
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

namespace {
bool createDeviceBackupUnlocked(const GlobalReadingStats& stats, std::string* createdPath = nullptr) {
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
  if (!verifiedFile(target.c_str(), *bytes)) return false;
  if (createdPath) *createdPath = target;
  return true;
}
}  // namespace

bool statsbackup::createDeviceBackup(const GlobalReadingStats& stats) {
  const auto save = [](void* raw) {
    const auto& input = **static_cast<const GlobalReadingStats**>(raw);
    return readingstats::normalStatsAllowed() && createDeviceBackupUnlocked(input);
  };
  const GlobalReadingStats* input = &stats;
  return Storage.withStorageLock(save, &input);
}

statsbackup::AutomaticBackupResult statsbackup::createAutomaticDeviceBackup(const ReadingStatsDate& date) {
  struct Context {
    const ReadingStatsDate& date;
    AutomaticBackupResult result = AutomaticBackupResult::Unavailable;
  } context{date};
  const auto backup = [](void* raw) {
    auto& input = *static_cast<Context*>(raw);
    const bool undated = input.date.year == 0 && input.date.month == 0 && input.date.day == 0;
    if ((!undated && !input.date.isValid()) || !readingstats::normalStatsAllowed() || StatsSessionLease::anyActive() ||
        Storage.lookupPath(DEVICE_STATS) != HalStorage::PathStatus::Present)
      return true;
    const auto current = GlobalReadingStats::loadReadOnly();
    auto bytes = makeUniqueNoThrow<std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE>>();
    if (!bytes || !current.writable || !current.encode(*bytes) || !matchesPrimary(current) ||
        !Storage.settingsChainEligible(DEVICE_STATS))
      return true;
    const auto directory = Storage.lookupPath(DIRECTORY);
    if (directory == HalStorage::PathStatus::Failed ||
        (directory == HalStorage::PathStatus::Missing && !Storage.mkdir(DIRECTORY)))
      return true;
    char filename[64];
    snprintf(filename, sizeof(filename), "stats_cp_%04u%02u%02u_%c_%016llx.bin", static_cast<unsigned>(input.date.year),
             static_cast<unsigned>(input.date.month), static_cast<unsigned>(input.date.day), undated ? 'U' : 'A',
             static_cast<unsigned long long>(hashBytes(*bytes)));
    const std::string target = std::string(DIRECTORY) + "/" + filename;
    const auto status = Storage.lookupPath(target.c_str());
    if (status == HalStorage::PathStatus::Present) {
      if (verifiedFile(target.c_str(), *bytes) && Storage.settingsChainEligible(target.c_str()) &&
          Storage.distinctSettingsChains(DEVICE_STATS, target.c_str()))
        input.result = AutomaticBackupResult::AlreadyPresent;
      return true;
    }
    if (status != HalStorage::PathStatus::Missing) return true;
    HalFile file = Storage.open(target.c_str(), O_WRONLY | O_CREAT | O_EXCL);
    if (!file) return true;
    const bool written = file.write(bytes->data(), bytes->size()) == bytes->size();
    const bool synced = written && file.sync();
    const bool closed = file.close();
    if (written && synced && closed && verifiedFile(target.c_str(), *bytes) &&
        Storage.settingsChainEligible(target.c_str()) && Storage.distinctSettingsChains(DEVICE_STATS, target.c_str()))
      input.result = AutomaticBackupResult::Saved;
    return true;
  };
  return Storage.withStorageLock(backup, &context) ? context.result : AutomaticBackupResult::Unavailable;
}

statsbackup::BackupIntegrity statsbackup::inspectDeviceBackup(const std::string& filename, GlobalReadingStats& output) {
  uint64_t expectedHash = 0;
  const bool checked = backupFilenameHash(filename, expectedHash);
  if (!checked && !legacyBackupFilename(filename)) return BackupIntegrity::Unavailable;
  struct Context {
    const std::string& filename;
    uint64_t expectedHash;
    bool checked;
    GlobalReadingStats& output;
    BackupIntegrity result = BackupIntegrity::Unavailable;
  } context{filename, expectedHash, checked, output};
  const auto read = [](void* raw) {
    auto& input = *static_cast<Context*>(raw);
    if (!readingstats::normalStatsAllowed()) return false;
    const std::string path = std::string(DIRECTORY) + "/" + input.filename;
    HalFile file;
    if (Storage.lookupPath(path.c_str()) != HalStorage::PathStatus::Present ||
        !Storage.openFileForRead("STATS", path, file))
      return false;
    std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> bytes{};
    const size_t size = file.fileSize();
    const bool readOk = size >= GlobalReadingStats::MIN_SUPPORTED_FILE_SIZE && size <= bytes.size() &&
                        (!input.checked || size == bytes.size()) &&
                        file.read(bytes.data(), size) == static_cast<int>(size);
    const bool closed = file.close();
    if (!readOk || !closed || (input.checked && hashBytes(bytes) != input.expectedHash)) return false;
    GlobalReadingStats decoded;
    if (!GlobalReadingStats::decode(bytes.data(), size, decoded)) return false;
    input.output = decoded;
    input.result = input.checked ? BackupIntegrity::Checked : BackupIntegrity::LegacyUnverified;
    return true;
  };
  if (!Storage.withStorageLock(read, &context)) return BackupIntegrity::Unavailable;
  return context.result;
}

bool statsbackup::readDeviceBackup(const std::string& filename, GlobalReadingStats& output) {
  return inspectDeviceBackup(filename, output) != BackupIntegrity::Unavailable;
}

statsbackup::RestoreResult statsbackup::restoreDeviceBackup(const std::string& filename, const uint8_t* expectedCurrent,
                                                            size_t expectedSize, const uint8_t* expectedBackup,
                                                            size_t expectedBackupSize) {
  struct Context {
    const std::string& filename;
    const uint8_t* expectedCurrent;
    size_t expectedSize;
    const uint8_t* expectedBackup;
    size_t expectedBackupSize;
    RestoreResult result = RestoreResult::Unavailable;
  } context{filename, expectedCurrent, expectedSize, expectedBackup, expectedBackupSize};
  const auto restore = [](void* raw) {
    auto& input = *static_cast<Context*>(raw);
    if (!readingstats::normalStatsAllowed()) return true;
    if (StatsSessionLease::anyActive()) {
      input.result = RestoreResult::Busy;
      return true;
    }
    GlobalReadingStats previous = GlobalReadingStats::loadReadOnly();
    GlobalReadingStats replacement;
    std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> previousBytes{};
    if (input.expectedCurrent && (input.expectedSize != previousBytes.size() || !previous.encode(previousBytes) ||
                                  memcmp(previousBytes.data(), input.expectedCurrent, previousBytes.size()) != 0))
      return true;
    if (!input.expectedCurrent && input.expectedSize != 0) return true;
    const std::string source = std::string(DIRECTORY) + "/" + input.filename;
    const auto priorBackup = Storage.lookupPath(DEVICE_STATS_BACKUP);
    if (!previous.writable || Storage.lookupPath(DEVICE_STATS) != HalStorage::PathStatus::Present ||
        !readDeviceBackup(input.filename, replacement) || !Storage.settingsChainEligible(DEVICE_STATS) ||
        !Storage.settingsChainEligible(source.c_str()) ||
        !Storage.distinctSettingsChains(DEVICE_STATS, source.c_str()) ||
        priorBackup == HalStorage::PathStatus::Failed ||
        (priorBackup == HalStorage::PathStatus::Present &&
         (!Storage.settingsChainEligible(DEVICE_STATS_BACKUP) ||
          !Storage.distinctSettingsChains(DEVICE_STATS, DEVICE_STATS_BACKUP) ||
          !Storage.distinctSettingsChains(source.c_str(), DEVICE_STATS_BACKUP))))
      return true;
    std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> replacementBytes{};
    if (input.expectedBackup &&
        (input.expectedBackupSize != replacementBytes.size() || !replacement.encode(replacementBytes) ||
         memcmp(replacementBytes.data(), input.expectedBackup, replacementBytes.size()) != 0))
      return true;
    if (!input.expectedBackup && input.expectedBackupSize != 0) return true;
    std::string beforePath;
    if (!createDeviceBackupUnlocked(previous, &beforePath) || !Storage.settingsChainEligible(beforePath.c_str()) ||
        !Storage.distinctSettingsChains(DEVICE_STATS, beforePath.c_str()) ||
        !Storage.distinctSettingsChains(source.c_str(), beforePath.c_str()) ||
        (priorBackup == HalStorage::PathStatus::Present &&
         !Storage.distinctSettingsChains(DEVICE_STATS_BACKUP, beforePath.c_str()))) {
      input.result = RestoreResult::BackupFailed;
      return true;
    }
    input.result = replacement.save() ? RestoreResult::Restored : RestoreResult::WriteUncertain;
    return true;
  };
  if (!Storage.withStorageLock(restore, &context)) return RestoreResult::Unavailable;
  return context.result;
}

statsbackup::RestoreResult statsbackup::resetDeviceStats(const uint8_t* expectedCurrent, size_t expectedSize) {
  struct Context {
    const uint8_t* expectedCurrent;
    size_t expectedSize;
    RestoreResult result = RestoreResult::Unavailable;
  } context{expectedCurrent, expectedSize};
  const auto reset = [](void* raw) {
    auto& input = *static_cast<Context*>(raw);
    if (!readingstats::normalStatsAllowed()) return true;
    if (StatsSessionLease::anyActive()) {
      input.result = RestoreResult::Busy;
      return true;
    }
    const GlobalReadingStats previous = GlobalReadingStats::loadReadOnly();
    std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> bytes{};
    const auto priorBackup = Storage.lookupPath(DEVICE_STATS_BACKUP);
    if (!previous.writable || !previous.encode(bytes) ||
        Storage.lookupPath(DEVICE_STATS) != HalStorage::PathStatus::Present ||
        !Storage.settingsChainEligible(DEVICE_STATS) || priorBackup == HalStorage::PathStatus::Failed ||
        (priorBackup == HalStorage::PathStatus::Present &&
         (!Storage.settingsChainEligible(DEVICE_STATS_BACKUP) ||
          !Storage.distinctSettingsChains(DEVICE_STATS, DEVICE_STATS_BACKUP))) ||
        (input.expectedCurrent &&
         (input.expectedSize != bytes.size() || memcmp(input.expectedCurrent, bytes.data(), bytes.size()) != 0)) ||
        (!input.expectedCurrent && input.expectedSize != 0))
      return true;
    std::string beforePath;
    if (!createDeviceBackupUnlocked(previous, &beforePath) || !Storage.settingsChainEligible(beforePath.c_str()) ||
        !Storage.distinctSettingsChains(DEVICE_STATS, beforePath.c_str()) ||
        (priorBackup == HalStorage::PathStatus::Present &&
         !Storage.distinctSettingsChains(DEVICE_STATS_BACKUP, beforePath.c_str()))) {
      input.result = RestoreResult::BackupFailed;
      return true;
    }
    input.result = GlobalReadingStats{}.save() ? RestoreResult::Restored : RestoreResult::WriteUncertain;
    return true;
  };
  if (!Storage.withStorageLock(reset, &context)) return RestoreResult::Unavailable;
  return context.result;
}
