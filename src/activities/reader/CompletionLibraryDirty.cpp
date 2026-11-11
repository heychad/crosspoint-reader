#include "CompletionLibraryDirty.h"

#include <HalStorage.h>

namespace readingstats::completionmove {
namespace {
constexpr char MARKER[] = "/.crosspoint/library.dirty";

bool markLocked(void*) {
  const auto status = Storage.lookupPath(MARKER);
  if (status == HalStorage::PathStatus::Failed) return false;
  const bool create = status == HalStorage::PathStatus::Missing;
  HalFile marker = Storage.openChecked(MARKER, create ? O_WRONLY | O_CREAT | O_EXCL : O_WRONLY);
  if (!marker) return false;
  const bool regular = !marker.isDirectory();
  const uint8_t value = 1;
  const bool written = regular && (!create || marker.write(&value, 1) == 1);
  const bool synced = written && marker.sync();
  const bool closed = marker.close();
  return regular && written && synced && closed && Storage.lookupPath(MARKER) == HalStorage::PathStatus::Present;
}
}  // namespace

bool markLibraryDirtyForMove() { return Storage.withStorageLock(markLocked, nullptr); }
}  // namespace readingstats::completionmove
