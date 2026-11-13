#include "MoveReferenceSafeWrite.h"

#include <HalStorage.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace movereference {
namespace {
bool matches(const char* path, const char* bytes, size_t size) {
  HalFile file = Storage.openChecked(path, O_RDONLY);
  if (!file) return false;
  bool equal = !file.isDirectory() && file.fileSize64() == size;
  char buffer[64];
  for (size_t offset = 0; equal && offset < size;) {
    const size_t count = std::min(sizeof(buffer), size - offset);
    equal = file.read(buffer, count) == static_cast<int>(count) && std::memcmp(buffer, bytes + offset, count) == 0;
    offset += count;
  }
  return file.close() && equal;
}

struct Context {
  const char* path;
  const char* bytes;
  size_t size;
  bool sleepPreferences;
  bool finishPublishedOnly = false;
};

bool archiveBackup(const std::string& primary, const std::string& backup) {
  if (!Storage.settingsChainEligible(backup.c_str()) ||
      !Storage.distinctSettingsChains(primary.c_str(), backup.c_str()))
    return false;
  char suffix[24];
  for (unsigned index = 0; index < 10000; ++index) {
    std::snprintf(suffix, sizeof(suffix), ".move-history-%04u", index);
    const std::string archive = primary + suffix;
    const auto status = Storage.lookupPath(archive.c_str());
    if (status == HalStorage::PathStatus::Failed) return false;
    if (status == HalStorage::PathStatus::Present) continue;
    if (!Storage.rename(backup.c_str(), archive.c_str())) return false;
    return Storage.lookupPath(backup.c_str()) == HalStorage::PathStatus::Missing &&
           Storage.lookupPath(archive.c_str()) == HalStorage::PathStatus::Present &&
           Storage.distinctSettingsChains(primary.c_str(), archive.c_str());
  }
  return false;
}

bool writeLocked(void* raw) {
  const auto& context = *static_cast<Context*>(raw);
  const std::string primary(context.path);
  const std::string temporary = primary + (context.sleepPreferences ? ".sleep-tmp" : ".tmp");
  const std::string backup = primary + (context.sleepPreferences ? ".sleep-bak" : ".bak");
  if (primary == "/.crosspoint/state.json") {
    const std::string otherTemporary = primary + (context.sleepPreferences ? ".tmp" : ".sleep-tmp");
    const std::string otherBackup = primary + (context.sleepPreferences ? ".bak" : ".sleep-bak");
    if (Storage.lookupPath(otherTemporary.c_str()) != HalStorage::PathStatus::Missing ||
        Storage.lookupPath(otherBackup.c_str()) != HalStorage::PathStatus::Missing)
      return false;
  }
  const auto current = Storage.lookupPath(primary.c_str());
  const auto backupStatus = Storage.lookupPath(backup.c_str());
  if (current == HalStorage::PathStatus::Failed || backupStatus == HalStorage::PathStatus::Failed ||
      Storage.lookupPath(temporary.c_str()) != HalStorage::PathStatus::Missing ||
      (context.sleepPreferences && backupStatus != HalStorage::PathStatus::Missing))
    return false;
  if (current == HalStorage::PathStatus::Present) {
    if (!Storage.settingsChainEligible(primary.c_str())) return false;
    if (matches(primary.c_str(), context.bytes, context.size))
      return backupStatus == HalStorage::PathStatus::Missing || archiveBackup(primary, backup);
  }
  if (backupStatus != HalStorage::PathStatus::Missing || context.finishPublishedOnly) return false;
  HalFile staged = Storage.openChecked(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL);
  if (!staged) return false;
  const bool written = staged.write(context.bytes, context.size) == context.size;
  const bool synced = staged.sync();
  const bool closed = staged.close();
  if (!written || !synced || !closed || !matches(temporary.c_str(), context.bytes, context.size) ||
      !Storage.settingsChainEligible(temporary.c_str()))
    return false;
  if (current == HalStorage::PathStatus::Present) {
    if (!Storage.distinctSettingsChains(primary.c_str(), temporary.c_str()) ||
        !Storage.rename(primary.c_str(), backup.c_str()) ||
        Storage.lookupPath(primary.c_str()) != HalStorage::PathStatus::Missing ||
        Storage.lookupPath(backup.c_str()) != HalStorage::PathStatus::Present ||
        !Storage.distinctSettingsChains(backup.c_str(), temporary.c_str()))
      return false;
  }
  if (!Storage.rename(temporary.c_str(), primary.c_str()) ||
      Storage.lookupPath(temporary.c_str()) != HalStorage::PathStatus::Missing ||
      Storage.lookupPath(primary.c_str()) != HalStorage::PathStatus::Present ||
      !matches(primary.c_str(), context.bytes, context.size))
    return false;
  if (current != HalStorage::PathStatus::Present) return true;
  if (context.sleepPreferences)
    return Storage.settingsChainEligible(backup.c_str()) &&
           Storage.distinctSettingsChains(primary.c_str(), backup.c_str()) && Storage.remove(backup.c_str()) &&
           Storage.lookupPath(backup.c_str()) == HalStorage::PathStatus::Missing;
  return archiveBackup(primary, backup);
}
}  // namespace

bool write(const char* path, const char* bytes, size_t size, bool finishPublishedOnly) {
  if (!path || !bytes || size == 0 || size > 65536 ||
      (std::strcmp(path, "/.crosspoint/state.json") != 0 && std::strcmp(path, "/.crosspoint/recent.json") != 0))
    return false;
  Context context{path, bytes, size, false, finishPublishedOnly};
  return Storage.withStorageLock(writeLocked, &context);
}
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
bool writeSleepPreferences(const char* bytes, size_t size) {
  if (!bytes || size == 0 || size > 65536) return false;
  Context context{"/.crosspoint/state.json", bytes, size, true};
  return Storage.withStorageLock(writeLocked, &context);
}

namespace {
constexpr char STATE_PRIMARY[] = "/.crosspoint/state.json";
constexpr char SLEEP_TEMP[] = "/.crosspoint/state.json.sleep-tmp";
constexpr char SLEEP_BACKUP[] = "/.crosspoint/state.json.sleep-bak";

bool archiveSleepArtifact(const char* source) {
  if (!Storage.settingsChainEligible(source) || !Storage.distinctSettingsChains(STATE_PRIMARY, source)) return false;
  char destination[64];
  for (unsigned index = 0; index < 10000; ++index) {
    std::snprintf(destination, sizeof(destination), "/.crosspoint/state.sleep-recovered-%04u", index);
    const auto status = Storage.lookupPath(destination);
    if (status == HalStorage::PathStatus::Failed) return false;
    if (status == HalStorage::PathStatus::Present) continue;
    if (!Storage.rename(source, destination)) return false;
    return Storage.lookupPath(source) == HalStorage::PathStatus::Missing &&
           Storage.lookupPath(destination) == HalStorage::PathStatus::Present &&
           Storage.distinctSettingsChains(STATE_PRIMARY, destination);
  }
  return false;
}

bool recoverSleepLocked(void* raw) {
  auto validate = *static_cast<bool (**)(const char*)>(raw);
  using Status = HalStorage::PathStatus;
  if (Storage.lookupPath("/.crosspoint/state.json.tmp") != Status::Missing ||
      Storage.lookupPath("/.crosspoint/state.json.bak") != Status::Missing)
    return false;
  auto primary = Storage.lookupPath(STATE_PRIMARY);
  auto backup = Storage.lookupPath(SLEEP_BACKUP);
  const auto temp = Storage.lookupPath(SLEEP_TEMP);
  if (primary == Status::Failed || backup == Status::Failed || temp == Status::Failed) return false;
  if (backup == Status::Missing && temp == Status::Missing) return false;
  if (primary == Status::Present) {
    if (!Storage.settingsChainEligible(STATE_PRIMARY) || !validate(STATE_PRIMARY)) return false;
  } else {
    if (backup != Status::Present || !Storage.settingsChainEligible(SLEEP_BACKUP) || !validate(SLEEP_BACKUP))
      return false;
    if (temp == Status::Present && !Storage.distinctSettingsChains(SLEEP_BACKUP, SLEEP_TEMP)) return false;
    if (!Storage.rename(SLEEP_BACKUP, STATE_PRIMARY) || Storage.lookupPath(SLEEP_BACKUP) != Status::Missing ||
        Storage.lookupPath(STATE_PRIMARY) != Status::Present || !validate(STATE_PRIMARY))
      return false;
    backup = Status::Missing;
    primary = Status::Present;
  }
  if (backup == Status::Present && temp == Status::Present && !Storage.distinctSettingsChains(SLEEP_BACKUP, SLEEP_TEMP))
    return false;
  if (temp == Status::Present && !archiveSleepArtifact(SLEEP_TEMP)) return false;
  if (backup == Status::Present && !archiveSleepArtifact(SLEEP_BACKUP)) return false;
  return primary == Status::Present;
}
}  // namespace

bool recoverSleepPreferences(bool (*isValidState)(const char*)) {
  if (!isValidState) return false;
  return Storage.withStorageLock(recoverSleepLocked, &isValidState);
}
#endif
}  // namespace movereference
