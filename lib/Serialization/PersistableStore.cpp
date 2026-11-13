#include "PersistableStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <ObfuscationUtils.h>

#include <algorithm>
#include <cstring>
#include <limits>

#include "MoveReferenceSafeWrite.h"

bool PersistableStoreBase::writeDocToFile(const char* path, const JsonDocument& doc) {
  Storage.mkdir("/.crosspoint");
  String json;
  serializeJson(doc, json);
  if (!Storage.writeFile(path, json)) {
    LOG_ERR("PERSIST", "Failed to write %s", path);
    return false;
  }
  return true;
}

bool PersistableStoreBase::readDocFromFile(const char* path, JsonDocument& doc, size_t maxBytes) {
  if (maxBytes != 0) {
    HalFile file;
    if (!Storage.openFileForRead("PERSIST", path, file)) return false;
    const size_t length = file.fileSize();
    if (length == 0 || length > maxBytes) return false;
    auto buffer = makeUniqueNoThrow<char[]>(length + 1);
    if (!buffer) {
      LOG_ERR("PERSIST", "Could not allocate %u bytes for %s", static_cast<unsigned>(length + 1), path);
      return false;
    }
    if (file.read(buffer.get(), length) != static_cast<int>(length) || !file.close()) return false;
    // Const input makes the document own its strings after this buffer is released.
    return !deserializeJson(doc, static_cast<const char*>(buffer.get()), length);
  }
  if (!Storage.exists(path)) {
    return false;  // Expected on first boot — not an error.
  }
  String json = Storage.readFile(path);
  if (json.isEmpty()) {
    LOG_ERR("PERSIST", "Failed to read %s (empty)", path);
    return false;
  }
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("PERSIST", "JSON parse error in %s: %s", path, error.c_str());
    return false;
  }
  return true;
}

namespace {
bool matchesFile(const std::string& path, const char* expected, size_t expectedSize) {
  if (!Storage.exists(path.c_str())) return false;
  HalFile file;
  if (!Storage.openFileForRead("PERSIST", path, file)) return false;
  if (file.fileSize() != expectedSize) return false;
  char buffer[64];
  size_t offset = 0;
  while (offset < expectedSize) {
    const size_t length = std::min(sizeof(buffer), expectedSize - offset);
    if (file.read(buffer, length) != static_cast<int>(length) || memcmp(buffer, expected + offset, length) != 0)
      return false;
    offset += length;
  }
  return file.close();
}
}  // namespace

bool PersistableStoreBase::writeDocToFileAtomic(const char* path, const JsonDocument& doc, size_t maxBytes) {
  const size_t expected = measureJson(doc);
  if (doc.overflowed() || expected == 0 || expected > maxBytes) return false;
  auto json = makeUniqueNoThrow<char[]>(expected + 1);
  if (!json) {
    LOG_ERR("PERSIST", "Could not allocate %u bytes for %s", static_cast<unsigned>(expected + 1), path);
    return false;
  }
  if (serializeJson(doc, json.get(), expected + 1) != expected) return false;
  if (matchesFile(path, json.get(), expected)) return true;
  const std::string temporary = std::string(path) + ".tmp";
  const std::string backup = std::string(path) + ".bak";
  if (Storage.exists(temporary.c_str()) && !Storage.remove(temporary.c_str())) return false;
  HalFile file;
  if (!Storage.openFileForWrite("PERSIST", temporary, file)) return false;
  const bool written = file.write(json.get(), expected) == expected;
  file.flush();
  const bool closed = file.close();
  if (!written || !closed || !matchesFile(temporary, json.get(), expected)) return false;
  const bool hadOriginal = Storage.exists(path);
  if (hadOriginal) {
    if (Storage.exists(backup.c_str()) && !Storage.remove(backup.c_str())) return false;
    if (!Storage.rename(path, backup.c_str())) return false;
  }
  if (!Storage.rename(temporary.c_str(), path)) {
    if (hadOriginal && !Storage.rename(backup.c_str(), path)) {
      LOG_ERR("PERSIST", "Could not restore %s from its backup", path);
    }
    return false;
  }
  if (Storage.exists(backup.c_str())) Storage.remove(backup.c_str());
  return true;
}

bool PersistableStoreBase::writeDocToFileForMove(const char* path, const JsonDocument& doc, bool finishPublishedOnly) {
  const size_t expected = measureJson(doc);
  if (doc.overflowed() || expected == 0 || expected > 65536) return false;
  auto json = makeUniqueNoThrow<char[]>(expected + 1);
  if (!json || serializeJson(doc, json.get(), expected + 1) != expected) return false;
  return movereference::write(path, json.get(), expected, finishPublishedOnly);
}

#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
bool PersistableStoreBase::writeStateSleepPreferences(const JsonDocument& doc) {
  const size_t expected = measureJson(doc);
  if (doc.overflowed() || expected == 0 || expected > 65536) return false;
  auto json = makeUniqueNoThrow<char[]>(expected + 1);
  if (!json || serializeJson(doc, json.get(), expected + 1) != expected) return false;
  return movereference::writeSleepPreferences(json.get(), expected);
}
#endif

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave) {
  bool valid = false;
  return extractPassword(doc, needsResave, std::numeric_limits<size_t>::max(), valid);
}

std::string PersistableStoreBase::extractPassword(JsonVariantConst doc, bool& needsResave, const size_t maxLength,
                                                  bool& valid) {
  valid = true;
  bool ok = false;
  bool tooLong = false;
  std::string pass = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", maxLength, &ok, &tooLong);
  if (tooLong) {
    valid = false;
    return "";
  }
  if (!ok) {
    // Deobfuscation failed — fall back to legacy plaintext password.
    const char* legacyPassword = doc["password"] | "";
    const size_t legacyLength = strlen(legacyPassword);
    if (legacyLength > maxLength) {
      valid = false;
      return "";
    }
    pass.assign(legacyPassword, legacyLength);
    if (!pass.empty()) needsResave = true;
  }
  // A successfully decoded empty string is a legitimate value; preserve as-is.
  return pass;
}
