#include "StatsSafeReplace.h"

#include <HalStorage.h>

#include <algorithm>
#include <cstring>

namespace statsreplace {
namespace {
enum class Content { Match, Different, Unavailable };
Content verifiedContent(const char* module, const char* path, const uint8_t* expected, size_t size) {
  HalFile file;
  if (!Storage.openFileForRead(module, path, file)) return Content::Unavailable;
  Content result = file.fileSize() == size ? Content::Match : Content::Different;
  uint8_t chunk[64];
  for (size_t offset = 0; result == Content::Match && offset < size;) {
    const size_t count = std::min(sizeof(chunk), size - offset);
    if (file.read(chunk, count) != static_cast<int>(count))
      result = Content::Unavailable;
    else if (std::memcmp(chunk, expected + offset, count) != 0)
      result = Content::Different;
    offset += count;
  }
  return file.close() ? result : Content::Unavailable;
}
struct Context {
  const char* module;
  const std::string& path;
  const uint8_t* bytes;
  size_t size;
  bool retainBackup;
};
bool saveLocked(void* input) {
  const auto& c = *static_cast<Context*>(input);
  if (!c.module || !c.bytes || c.size == 0 || c.size > 512) return false;
  const std::string temp = c.path + ".tmp";
  const std::string backup = c.path + ".bak";
  const auto currentStatus = Storage.lookupPath(c.path.c_str());
  const auto tempStatus = Storage.lookupPath(temp.c_str());
  const auto backupStatus = Storage.lookupPath(backup.c_str());
  if (currentStatus == HalStorage::PathStatus::Failed || tempStatus == HalStorage::PathStatus::Failed ||
      backupStatus == HalStorage::PathStatus::Failed)
    return false;
  if (tempStatus == HalStorage::PathStatus::Present) {
    // A first save may have failed before publishing. Retry only when the
    // staged bytes match this exact save and no other transaction files exist.
    if (currentStatus != HalStorage::PathStatus::Missing || backupStatus != HalStorage::PathStatus::Missing ||
        verifiedContent(c.module, temp.c_str(), c.bytes, c.size) != Content::Match ||
        !Storage.rename(temp.c_str(), c.path.c_str()))
      return false;
    return Storage.lookupPath(temp.c_str()) == HalStorage::PathStatus::Missing &&
           Storage.lookupPath(c.path.c_str()) == HalStorage::PathStatus::Present &&
           verifiedContent(c.module, c.path.c_str(), c.bytes, c.size) == Content::Match;
  }
  // A backup can be the only surviving copy or can alias the primary after a
  // failed rename. Never delete it until identity checks prove it is distinct.
  if (backupStatus == HalStorage::PathStatus::Present && currentStatus == HalStorage::PathStatus::Present &&
      !Storage.distinctFirstClusters(c.path.c_str(), backup.c_str()))
    return false;
  if (currentStatus == HalStorage::PathStatus::Present) {
    const auto content = verifiedContent(c.module, c.path.c_str(), c.bytes, c.size);
    if (content == Content::Match) return true;
    if (content == Content::Unavailable) return false;
  }
  if (backupStatus == HalStorage::PathStatus::Present && currentStatus == HalStorage::PathStatus::Present &&
      !Storage.remove(backup.c_str()))
    return false;

  HalFile file = Storage.open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL);
  if (!file) return false;
  const bool written = file.write(c.bytes, c.size) == c.size;
  const bool synced = written && file.sync();
  const bool closed = file.close();
  if (!written || !synced || !closed || verifiedContent(c.module, temp.c_str(), c.bytes, c.size) != Content::Match)
    return false;

  if (currentStatus == HalStorage::PathStatus::Missing && backupStatus == HalStorage::PathStatus::Present &&
      !Storage.distinctFirstClusters(temp.c_str(), backup.c_str()))
    return false;
  if (currentStatus == HalStorage::PathStatus::Present) {
    if (!Storage.rename(c.path.c_str(), backup.c_str())) return false;
    if (Storage.lookupPath(c.path.c_str()) != HalStorage::PathStatus::Missing ||
        Storage.lookupPath(backup.c_str()) != HalStorage::PathStatus::Present ||
        !Storage.distinctFirstClusters(temp.c_str(), backup.c_str()))
      return false;
  }
  if (!Storage.rename(temp.c_str(), c.path.c_str())) {
    // Attempt the old rollback only when the primary is definitely absent. A
    // failed rollback can itself create aliases, so leave all artifacts intact.
    if (currentStatus == HalStorage::PathStatus::Present &&
        Storage.lookupPath(c.path.c_str()) == HalStorage::PathStatus::Missing &&
        Storage.lookupPath(backup.c_str()) == HalStorage::PathStatus::Present)
      Storage.rename(backup.c_str(), c.path.c_str());
    // The staged file contains the unsaved update even after a successful rollback.
    // Preserve it for recovery rather than discarding pending reading time.
    return false;
  }
  if (Storage.lookupPath(temp.c_str()) != HalStorage::PathStatus::Missing ||
      Storage.lookupPath(c.path.c_str()) != HalStorage::PathStatus::Present)
    return false;
  if ((currentStatus == HalStorage::PathStatus::Present || backupStatus == HalStorage::PathStatus::Present) &&
      (Storage.lookupPath(backup.c_str()) != HalStorage::PathStatus::Present ||
       !Storage.distinctFirstClusters(c.path.c_str(), backup.c_str())))
    return false;
  if (verifiedContent(c.module, c.path.c_str(), c.bytes, c.size) != Content::Match) return false;
  // Global statistics retain a backup; book statistics normally remove it only
  // after the new file and both identities have been checked.
  if (!c.retainBackup && currentStatus == HalStorage::PathStatus::Present && !Storage.remove(backup.c_str()))
    return false;
  return true;
}
}  // namespace
bool save(const char* module, const std::string& path, const uint8_t* bytes, size_t size, bool retainBackup) {
  Context context{module, path, bytes, size, retainBackup};
  return Storage.withStorageLock(saveLocked, &context);
}
}  // namespace statsreplace
