#include "SettingsRecoveryProbe.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Memory.h>

#include "SettingsJsonEnvelope.h"

namespace settingsrecovery {
namespace {
constexpr size_t MAX_SETTINGS_BYTES = 32768;

Candidate inspect(const char* path) {
  if (!path || path[0] != '/') return Candidate::Unavailable;
  const auto status = Storage.lookupPath(path);
  if (status == HalStorage::PathStatus::Missing) return Candidate::Missing;
  if (status != HalStorage::PathStatus::Present) return Candidate::Unavailable;
  HalFile file = Storage.openChecked(path, O_RDONLY);
  if (!file) return Candidate::Unavailable;
  const uint64_t size = file.fileSize64();
  if (file.isDirectory() || size == 0 || size > MAX_SETTINGS_BYTES) {
    return file.close() ? Candidate::Invalid : Candidate::Unavailable;
  }
  auto buffer = makeUniqueNoThrow<char[]>(static_cast<size_t>(size) + 1);
  if (!buffer) {
    file.close();
    return Candidate::Unavailable;
  }
  const bool read = file.read(buffer.get(), static_cast<size_t>(size)) == static_cast<int>(size);
  const bool closed = file.close();
  if (!read || !closed) return Candidate::Unavailable;
  buffer[size] = '\0';
  if (!completeObjectEnvelope(buffer.get(), static_cast<size_t>(size))) return Candidate::Invalid;
  JsonDocument doc;
  const auto error = deserializeJson(doc, static_cast<const char*>(buffer.get()), static_cast<size_t>(size));
  if (error == DeserializationError::NoMemory || doc.overflowed()) return Candidate::Unavailable;
  return !error && doc.is<JsonObjectConst>() ? Candidate::JsonObject : Candidate::Invalid;
}

struct Context {
  const char* primary;
  const char* backup;
  const char* temporary;
  Snapshot result;
};
bool inspectLocked(void* raw) {
  auto& context = *static_cast<Context*>(raw);
  context.result.primary = inspect(context.primary);
  context.result.backup = inspect(context.backup);
  context.result.temporary = inspect(context.temporary);
  return true;
}
}  // namespace

Snapshot probe(const char* primary, const char* backup, const char* temporary) {
  Context context{primary, backup, temporary, {}};
  return Storage.withStorageLock(inspectLocked, &context) ? context.result : Snapshot{};
}
}  // namespace settingsrecovery
