#include "SettingsSnapshotCopy.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

#include "SettingsJsonEnvelope.h"

namespace settingsrecovery {
namespace {
constexpr size_t MAX_SETTINGS_BYTES = 32768;
struct Context {
  const char* source;
  const char* destination;
  CopyResult result = CopyResult::Failed;
};

bool copyLocked(void* raw) {
  auto& c = *static_cast<Context*>(raw);
  if (!c.source || !c.destination || c.source[0] != '/' || c.destination[0] != '/' ||
      std::strcmp(c.source, c.destination) == 0) {
    c.result = CopyResult::Refused;
    return true;
  }
  const auto srcStatus = Storage.lookupPath(c.source);
  const auto dstStatus = Storage.lookupPath(c.destination);
  if (srcStatus == HalStorage::PathStatus::Failed || dstStatus == HalStorage::PathStatus::Failed) return true;
  if (srcStatus != HalStorage::PathStatus::Present || dstStatus != HalStorage::PathStatus::Missing) {
    c.result = CopyResult::Refused;
    return true;
  }
  HalFile source = Storage.openChecked(c.source, O_RDONLY);
  if (!source) return true;
  const uint64_t size = source.fileSize64();
  if (source.isDirectory() || size == 0 || size > MAX_SETTINGS_BYTES) {
    c.result = source.close() ? CopyResult::Refused : CopyResult::Failed;
    return true;
  }
  auto bytes = makeUniqueNoThrow<char[]>(static_cast<size_t>(size) + 1);
  if (!bytes) {
    source.close();
    return true;
  }
  const bool read = source.read(bytes.get(), static_cast<size_t>(size)) == static_cast<int>(size);
  const bool sourceClosed = source.close();
  if (!read || !sourceClosed) return true;
  bytes[size] = '\0';
  if (!completeObjectEnvelope(bytes.get(), static_cast<size_t>(size))) {
    c.result = CopyResult::Refused;
    return true;
  }
  JsonDocument doc;
  const auto error = deserializeJson(doc, static_cast<const char*>(bytes.get()), static_cast<size_t>(size));
  if (error || doc.overflowed() || !doc.is<JsonObjectConst>()) {
    c.result = error == DeserializationError::NoMemory || doc.overflowed() ? CopyResult::Failed : CopyResult::Refused;
    return true;
  }
  HalFile destination = Storage.openChecked(c.destination, O_WRONLY | O_CREAT | O_EXCL);
  if (!destination) return true;
  const bool written = destination.write(bytes.get(), static_cast<size_t>(size)) == size;
  const bool synced = written && destination.sync();
  const bool closed = destination.close();
  if (!written || !synced || !closed) return true;
  HalFile check = Storage.openChecked(c.destination, O_RDONLY);
  if (!check) return true;
  bool same = !check.isDirectory() && check.fileSize64() == size;
  char chunk[64];
  for (size_t offset = 0; same && offset < size;) {
    const size_t amount = std::min(sizeof(chunk), static_cast<size_t>(size) - offset);
    same =
        check.read(chunk, amount) == static_cast<int>(amount) && std::memcmp(chunk, bytes.get() + offset, amount) == 0;
    offset += amount;
  }
  const bool checkClosed = check.close();
  if (same && checkClosed) c.result = CopyResult::Copied;
  return true;
}
}  // namespace

CopyResult copySnapshot(const char* source, const char* destination) {
  Context context{source, destination};
  return Storage.withStorageLock(copyLocked, &context) ? context.result : CopyResult::Failed;
}
}  // namespace settingsrecovery
