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

namespace {
using CheckedRead = PersistableStoreBase::CheckedRead;

CheckedRead readBoundedDoc(const char* path, HalFile& file, JsonDocument& doc, size_t maxBytes) {
  if (!file || file.isDirectory()) return CheckedRead::Unavailable;
  const uint64_t size = file.fileSize64();
  if (size > maxBytes || size > std::numeric_limits<int>::max()) return CheckedRead::Unavailable;
  if (size == 0) return file.close() ? CheckedRead::Malformed : CheckedRead::Unavailable;
  const size_t length = static_cast<size_t>(size);
  auto buffer = makeUniqueNoThrow<char[]>(length);
  if (!buffer) {
    LOG_ERR("PERSIST", "Could not allocate %u bytes for %s", static_cast<unsigned>(length), path);
    return CheckedRead::Unavailable;
  }
  const bool read = file.read(buffer.get(), length) == static_cast<int>(length);
  const bool closed = file.close();
  if (!read || !closed) return CheckedRead::Unavailable;
  // Const input makes the document own its strings after this buffer is released.
  const auto error = deserializeJson(doc, static_cast<const char*>(buffer.get()), length);
  if (!error) return CheckedRead::Loaded;
  if (error == DeserializationError::EmptyInput || error == DeserializationError::IncompleteInput ||
      error == DeserializationError::InvalidInput)
    return CheckedRead::Malformed;
  return CheckedRead::Unavailable;
}
}  // namespace

PersistableStoreBase::CheckedRead PersistableStoreBase::readCheckedDocFromFile(const char* path, JsonDocument& doc,
                                                                               size_t maxBytes) {
  HalFile file = Storage.openChecked(path, O_RDONLY);
  return readBoundedDoc(path, file, doc, maxBytes);
}

bool PersistableStoreBase::readDocFromFile(const char* path, JsonDocument& doc, size_t maxBytes) {
  if (maxBytes != 0) {
    HalFile file;
    return Storage.openFileForRead("PERSIST", path, file) &&
           readBoundedDoc(path, file, doc, maxBytes) == CheckedRead::Loaded;
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
enum class FileMatch { Equal, Different, Failed };

FileMatch matchesFile(const char* path, const char* expected, size_t expectedSize) {
  HalFile file = Storage.openChecked(path, O_RDONLY);
  if (!file) return FileMatch::Failed;
  bool equal = !file.isDirectory() && file.fileSize64() == expectedSize;
  bool read = true;
  char buffer[64];
  size_t offset = 0;
  while (equal && offset < expectedSize) {
    const size_t length = std::min(sizeof(buffer), expectedSize - offset);
    read = file.read(buffer, length) == static_cast<int>(length);
    equal = read && memcmp(buffer, expected + offset, length) == 0;
    offset += length;
  }
  if (!file.close() || !read) return FileMatch::Failed;
  return equal ? FileMatch::Equal : FileMatch::Different;
}

struct AtomicWrite {
  const char* primary;
  const char* temporary;
  const char* backup;
  const char* bytes;
  size_t size;
  bool useMoveChains;

  bool eligible(const char* path, bool allowUnallocatedEmpty = false) const {
    return useMoveChains ? Storage.moveChainEligible(path, allowUnallocatedEmpty) : Storage.settingsChainEligible(path);
  }

  bool distinct(const char* first, const char* second) const {
    return useMoveChains ? Storage.distinctMoveChains(first, second) : Storage.distinctSettingsChains(first, second);
  }

  bool renameChecked(const char* from, const char* to) const {
    return Storage.rename(from, to) && Storage.lookupPath(from) == HalStorage::PathStatus::Missing && eligible(to);
  }
};

bool writeAtomicLocked(void* raw) {
  const auto& c = *static_cast<const AtomicWrite*>(raw);
  const char* paths[] = {c.primary, c.temporary, c.backup};
  bool present[3];
  for (unsigned i = 0; i < 3; ++i) {
    const auto state = Storage.lookupPath(paths[i]);
    if (state == HalStorage::PathStatus::Failed) return false;
    present[i] = state == HalStorage::PathStatus::Present;
  }
  for (unsigned i = 0; i < 3; ++i) {
    const bool discardableEmptyStage = i == 1 && (present[0] || present[2]);
    if (present[i] && !c.eligible(paths[i], discardableEmptyStage)) return false;
    for (unsigned j = 0; j < i; ++j)
      if (present[i] && present[j] && !c.distinct(paths[i], paths[j])) return false;
  }
  // A failed rename can leave two names for one chain. Check every existing pair
  // before retiring a file, including when the primary already matches.
  if (!present[0] && (present[2] || present[1])) {
    const unsigned recovery = present[2] ? 2 : 1;
    if (!c.renameChecked(paths[recovery], c.primary)) return false;
    present[0] = true;
    present[recovery] = false;
  }
  if (present[0]) {
    const auto match = matchesFile(c.primary, c.bytes, c.size);
    if (match == FileMatch::Failed) return false;
    if (match == FileMatch::Equal) return true;
  }
  const auto stagedMatch = present[1] ? matchesFile(paths[1], c.bytes, c.size) : FileMatch::Different;
  if (stagedMatch == FileMatch::Failed) return false;
  if (present[2]) {
    if (!present[0] || !c.distinct(c.primary, paths[2]) || !Storage.remove(paths[2]) ||
        Storage.lookupPath(paths[2]) != HalStorage::PathStatus::Missing)
      return false;
  }
  const bool reuseStaged = stagedMatch == FileMatch::Equal;
  if (present[1] && !reuseStaged) {
    if (!present[0] || !c.distinct(c.primary, paths[1]) || !Storage.remove(paths[1]) ||
        Storage.lookupPath(paths[1]) != HalStorage::PathStatus::Missing)
      return false;
  }
  if (!reuseStaged) {
    HalFile file = Storage.openChecked(paths[1], O_WRONLY | O_CREAT | O_EXCL);
    if (!file) return false;
    const bool written = file.write(c.bytes, c.size) == c.size;
    const bool synced = written && file.sync();
    const bool closed = file.close();
    if (!written || !synced || !closed || matchesFile(paths[1], c.bytes, c.size) != FileMatch::Equal ||
        !c.eligible(paths[1]))
      return false;
  }
  if (present[0]) {
    if (!c.distinct(c.primary, paths[1]) || !c.renameChecked(c.primary, paths[2]) || !c.distinct(paths[2], paths[1]))
      return false;
  }
  if (!c.renameChecked(paths[1], c.primary)) {
    // Restore only when the failed publish left no destination or shared chain.
    if (present[0] && Storage.lookupPath(c.primary) == HalStorage::PathStatus::Missing &&
        Storage.lookupPath(paths[2]) == HalStorage::PathStatus::Present &&
        Storage.lookupPath(paths[1]) == HalStorage::PathStatus::Present && c.distinct(paths[2], paths[1]))
      c.renameChecked(paths[2], c.primary);
    return false;
  }
  if (matchesFile(c.primary, c.bytes, c.size) != FileMatch::Equal) return false;
  return !present[0] || (c.distinct(c.primary, paths[2]) && Storage.remove(paths[2]) &&
                         Storage.lookupPath(paths[2]) == HalStorage::PathStatus::Missing);
}
bool writeAtomicJson(AtomicWrite context, const JsonDocument& doc, size_t maxBytes) {
  const size_t expected = measureJson(doc);
  if (doc.overflowed() || expected == 0 || expected > maxBytes) return false;
  auto json = makeUniqueNoThrow<char[]>(expected + 1);
  if (!json) {
    LOG_ERR("PERSIST", "Could not allocate %u bytes for %s", static_cast<unsigned>(expected + 1), context.primary);
    return false;
  }
  if (serializeJson(doc, json.get(), expected + 1) != expected) return false;
  context.bytes = json.get();
  context.size = expected;
  return Storage.withStorageLock(writeAtomicLocked, &context);
}
}  // namespace

bool PersistableStoreBase::writeDocToFileAtomic(const char* path, const JsonDocument& doc, size_t maxBytes) {
  const auto temporary = std::string(path) + ".tmp";
  const auto backup = std::string(path) + ".bak";
  return writeAtomicJson({path, temporary.c_str(), backup.c_str(), nullptr, 0, false}, doc, maxBytes);
}

bool PersistableStoreBase::writeDocToFileAtomic(const char* path, const char* temporary, const char* backup,
                                                const JsonDocument& doc, size_t maxBytes) {
  return writeAtomicJson({path, temporary, backup, nullptr, 0, true}, doc, maxBytes);
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
