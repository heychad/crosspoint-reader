#include "SettingsSafeWrite.h"

#include <ArduinoJson.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "SettingsJsonAllocator.h"
#include "SettingsJsonEnvelope.h"
#include "SettingsRecoveryProbe.h"

namespace settingsrecovery {
namespace {
constexpr char PRIMARY[] = "/.crosspoint/settings.json";
constexpr char TEMP[] = "/.crosspoint/settings.json.tmp";
constexpr char BACKUP[] = "/.crosspoint/settings.json.bak";
constexpr size_t MAX_BYTES = 32768;

bool matches(const char* path, const char* bytes, size_t size) {
  HalFile file = Storage.openChecked(path, O_RDONLY);
  if (!file) return false;
  bool equal = !file.isDirectory() && file.fileSize64() == size;
  char buffer[64];
  for (size_t offset = 0; equal && offset < size;) {
    const size_t count = std::min(sizeof(buffer), size - offset);
    equal = file.read(buffer, count) == static_cast<int>(count) && !std::memcmp(buffer, bytes + offset, count);
    offset += count;
  }
  return file.close() && equal;
}

struct Context {
  const char* bytes;
  size_t size;
};

bool archiveBackup() {
  char destination[64];
  for (unsigned index = 1; index <= 99; ++index) {
    std::snprintf(destination, sizeof(destination), "/.crosspoint/settings.recovered-%02u.bak", index);
    const auto status = Storage.lookupPath(destination);
    if (status == HalStorage::PathStatus::Failed) return false;
    if (status == HalStorage::PathStatus::Present) continue;
    if (!Storage.rename(BACKUP, destination)) return false;
    return Storage.lookupPath(BACKUP) == HalStorage::PathStatus::Missing &&
           Storage.lookupPath(destination) == HalStorage::PathStatus::Present &&
           Storage.distinctSettingsChains(PRIMARY, destination);
  }
  return false;
}

bool promoteStaged(const Context& c, bool hasBackup, bool archiveExistingBackup) {
  if (!Storage.rename(TEMP, PRIMARY)) return false;
  if (Storage.lookupPath(TEMP) != HalStorage::PathStatus::Missing ||
      Storage.lookupPath(PRIMARY) != HalStorage::PathStatus::Present || !matches(PRIMARY, c.bytes, c.size))
    return false;
  if (!hasBackup) return true;
  if (Storage.lookupPath(BACKUP) != HalStorage::PathStatus::Present || !Storage.distinctSettingsChains(PRIMARY, BACKUP))
    return false;
  return archiveExistingBackup ? archiveBackup() : Storage.remove(BACKUP);
}

bool stageSettings(const Context& c) {
  HalFile staged = Storage.openChecked(TEMP, O_WRONLY | O_CREAT | O_EXCL);
  if (!staged) return false;
  const bool written = staged.write(c.bytes, c.size) == c.size;
  const bool synced = written && staged.sync();
  const bool closed = staged.close();
  return written && synced && closed && matches(TEMP, c.bytes, c.size) && Storage.settingsChainEligible(TEMP);
}

bool writeLocked(void* raw) {
  const auto& c = *static_cast<const Context*>(raw);
  const auto state = probe(PRIMARY, BACKUP, TEMP);
  const bool hadPrimary = state.primary == Candidate::JsonObject;
  const bool hadBackup = state.backup == Candidate::JsonObject;
  if (hadPrimary && hadBackup && state.temporary == Candidate::Missing && matches(PRIMARY, c.bytes, c.size) &&
      Storage.distinctSettingsChains(PRIMARY, BACKUP))
    return archiveBackup();
  if (state.primary == Candidate::Missing && hadBackup && state.temporary == Candidate::Missing &&
      matches(BACKUP, c.bytes, c.size) && Storage.settingsChainEligible(BACKUP)) {
    if (!stageSettings(c) || !Storage.distinctSettingsChains(BACKUP, TEMP)) return false;
    return promoteStaged(c, true, true);
  }
  if (state.temporary == Candidate::JsonObject && matches(TEMP, c.bytes, c.size) &&
      Storage.settingsChainEligible(TEMP)) {
    if (hadPrimary && state.backup == Candidate::Missing && Storage.distinctSettingsChains(PRIMARY, TEMP)) {
      if (!Storage.rename(PRIMARY, BACKUP) || Storage.lookupPath(PRIMARY) != HalStorage::PathStatus::Missing ||
          Storage.lookupPath(BACKUP) != HalStorage::PathStatus::Present ||
          !Storage.distinctSettingsChains(BACKUP, TEMP))
        return false;
      return promoteStaged(c, true, false);
    }
    if (state.primary == Candidate::Missing && hadBackup && Storage.distinctSettingsChains(BACKUP, TEMP))
      return promoteStaged(c, true, true);
    if (state.primary == Candidate::Missing && state.backup == Candidate::Missing)
      return promoteStaged(c, false, false);
    return false;
  }
  if ((!hadPrimary && state.decision() != Plan::FirstRun) || (hadPrimary && state.decision() != Plan::InspectPrimary))
    return false;
  if (hadPrimary && !Storage.settingsChainEligible(PRIMARY)) return false;
  if (hadPrimary && matches(PRIMARY, c.bytes, c.size)) return true;

  if (!stageSettings(c)) return false;
  if (hadPrimary) {
    if (!Storage.distinctSettingsChains(PRIMARY, TEMP) || !Storage.rename(PRIMARY, BACKUP)) return false;
    if (Storage.lookupPath(PRIMARY) != HalStorage::PathStatus::Missing ||
        Storage.lookupPath(BACKUP) != HalStorage::PathStatus::Present || !Storage.distinctSettingsChains(BACKUP, TEMP))
      return false;
  }
  return promoteStaged(c, hadPrimary, false);
}
}  // namespace

bool writeSettings(const char* bytes, size_t size) {
  if (!bytes || size == 0 || size > MAX_BYTES || !completeObjectEnvelope(bytes, size)) return false;
  {
    SettingsJsonAllocator allocator;
    JsonDocument doc(&allocator);
    const auto error = deserializeJson(doc, static_cast<const char*>(bytes), size);
    if (error || doc.overflowed() || !doc.is<JsonObjectConst>()) return false;
  }
  Context context{bytes, size};
  return Storage.withStorageLock(writeLocked, &context);
}
}  // namespace settingsrecovery
