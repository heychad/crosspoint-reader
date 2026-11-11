#include "CrossInkImportSnapshot.h"

#include <HalStorage.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace crossinkimport {
namespace {
constexpr char PRIMARY[] = "/.crosspoint/settings.json";
constexpr char BACKUP[] = "/.crosspoint/settings.json.bak";
constexpr char TEMP[] = "/.crosspoint/settings.json.tmp";
constexpr size_t MAX_BYTES = 32768;

bool matches(const char* path, const char* bytes, size_t size) {
  HalFile file = Storage.openChecked(path, O_RDONLY);
  if (!file) return false;
  bool equal = !file.isDirectory() && file.fileSize64() == size;
  char block[64];
  for (size_t offset = 0; equal && offset < size;) {
    const size_t count = std::min(sizeof(block), size - offset);
    equal = file.read(block, count) == static_cast<int>(count) && !std::memcmp(block, bytes + offset, count);
    offset += count;
  }
  return file.close() && equal;
}

struct Context {
  const char* expected;
  size_t size;
  char (&path)[SNAPSHOT_PATH_SIZE];
  bool saved = false;
};

bool snapshotLocked(void* raw) {
  auto& context = *static_cast<Context*>(raw);
  if (Storage.lookupPath(PRIMARY) != HalStorage::PathStatus::Present ||
      Storage.lookupPath(BACKUP) != HalStorage::PathStatus::Missing ||
      Storage.lookupPath(TEMP) != HalStorage::PathStatus::Missing || !Storage.settingsChainEligible(PRIMARY) ||
      !matches(PRIMARY, context.expected, context.size))
    return true;
  char candidate[SNAPSHOT_PATH_SIZE];
  for (unsigned index = 0; index < 64; ++index) {
    const int length = std::snprintf(candidate, sizeof(candidate), "/.crosspoint/settings-crossink-%02u.bak", index);
    if (length < 0 || static_cast<size_t>(length) >= sizeof(candidate)) return true;
    const auto status = Storage.lookupPath(candidate);
    if (status == HalStorage::PathStatus::Failed) return true;
    if (status == HalStorage::PathStatus::Present) continue;
    char pending[64], done[64];
    const int p = std::snprintf(pending, sizeof(pending), "/.crosspoint/crossink-import/.cip-%02u", index);
    const int d = std::snprintf(done, sizeof(done), "/.crosspoint/crossink-import/.cid-%02u", index);
    if (p < 0 || d < 0 || static_cast<size_t>(p) >= sizeof(pending) || static_cast<size_t>(d) >= sizeof(done))
      return true;
    const auto pendingStatus = Storage.lookupPath(pending), doneStatus = Storage.lookupPath(done);
    if (pendingStatus == HalStorage::PathStatus::Failed || doneStatus == HalStorage::PathStatus::Failed) return true;
    if (pendingStatus == HalStorage::PathStatus::Present || doneStatus == HalStorage::PathStatus::Present) continue;
    HalFile file = Storage.openChecked(candidate, O_WRONLY | O_CREAT | O_EXCL);
    if (!file) return true;
    const bool written = file.write(context.expected, context.size) == context.size;
    const bool synced = written && file.sync();
    const bool closed = file.close();
    if (!written || !synced || !closed || !matches(candidate, context.expected, context.size) ||
        !Storage.settingsChainEligible(candidate) || !Storage.distinctSettingsChains(PRIMARY, candidate))
      return true;
    std::memcpy(context.path, candidate, static_cast<size_t>(length) + 1);
    context.saved = true;
    return true;
  }
  return true;
}
}  // namespace

bool snapshotCurrentSettings(const char* expected, size_t size, char (&path)[SNAPSHOT_PATH_SIZE]) {
  path[0] = '\0';
  if (!expected || !size || size > MAX_BYTES) return false;
  Context context{expected, size, path};
  return Storage.withStorageLock(snapshotLocked, &context) && context.saved;
}
}  // namespace crossinkimport
