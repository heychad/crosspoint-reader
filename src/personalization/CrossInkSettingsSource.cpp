#include "CrossInkSettingsSource.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <utility>

namespace crossinkimport {
namespace {
struct ReadContext {
  JsonDocument& output;
  SourceResult result = SourceResult::Unavailable;
};

bool readLocked(void* raw) {
  auto& context = *static_cast<ReadContext*>(raw);
  const auto primary = Storage.lookupPath(SOURCE_PATH);
  const auto backup = Storage.lookupPath(BACKUP_PATH);
  const auto temporary = Storage.lookupPath(TEMP_PATH);
  if (primary == HalStorage::PathStatus::Failed || backup == HalStorage::PathStatus::Failed ||
      temporary == HalStorage::PathStatus::Failed)
    return true;
  if (backup == HalStorage::PathStatus::Present || temporary == HalStorage::PathStatus::Present) {
    context.result = SourceResult::RecoveryPresent;
    return true;
  }
  if (primary == HalStorage::PathStatus::Missing) {
    context.result = SourceResult::Missing;
    return true;
  }
  HalFile file;
  if (!Storage.openFileForRead("CROSSINK", SOURCE_PATH, file)) return true;
  const uint64_t length = file.fileSize64();
  if (file.isDirectory() || length == 0 || length > MAX_SOURCE_BYTES) {
    file.close();
    context.result = SourceResult::Invalid;
    return true;
  }
  auto bytes = makeUniqueNoThrow<char[]>(static_cast<size_t>(length) + 1);
  if (!bytes) {
    LOG_ERR("CROSSINK", "Could not allocate %u bytes for settings", static_cast<unsigned>(length + 1));
    file.close();
    return true;
  }
  const bool read = file.read(bytes.get(), static_cast<size_t>(length)) == static_cast<int>(length);
  const bool closed = file.close();
  if (!read || !closed) return true;
  JsonDocument parsed;
  const auto error = deserializeJson(parsed, static_cast<const char*>(bytes.get()), static_cast<size_t>(length),
                                     DeserializationOption::NestingLimit(16));
  if (error == DeserializationError::NoMemory || parsed.overflowed()) return true;
  if (error || !parsed.is<JsonObjectConst>()) {
    context.result = SourceResult::Invalid;
    return true;
  }
  context.output = std::move(parsed);
  context.result = SourceResult::Loaded;
  return true;
}
}  // namespace

SourceResult readSource(JsonDocument& output) {
  ReadContext context{output};
  return Storage.withStorageLock(readLocked, &context) ? context.result : SourceResult::Unavailable;
}
}  // namespace crossinkimport
