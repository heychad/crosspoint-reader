#include "StageInspect.h"

#include <cstdio>
#include <cstring>

#include "StageName.h"
#include "StageWriter.h"

namespace syncstage {
StageInspectResult inspectStage(HalStorage& storage, const char* name, uint64_t size, uint8_t (&digest)[32],
                                DigestTick tick, void* context) {
  if (!name || !validName(name) || size > StageWriter::MAX_BYTES || !tick) return StageInspectResult::Invalid;
  HalStorage::MutationSnapshot snapshot;
  if (!storage.beginMutationSnapshot(snapshot)) return StageInspectResult::Unavailable;
  if (!tick(context)) return StageInspectResult::Cancelled;
  char path[64];
  const int length = snprintf(path, sizeof(path), "/%s", name);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(path)) return StageInspectResult::Invalid;
  const auto status = storage.lookupPath(path);
  if (!storage.mutationSnapshotUnchanged(snapshot)) return StageInspectResult::Changed;
  if (status == HalStorage::PathStatus::Missing) return StageInspectResult::Missing;
  if (status != HalStorage::PathStatus::Present) return StageInspectResult::Unavailable;
  uint8_t result[32];
  const auto checked = hashStage(storage, name, size, result, tick, context);
  if (!storage.mutationSnapshotUnchanged(snapshot) || checked == DigestResult::Changed)
    return StageInspectResult::Changed;
  if (checked == DigestResult::Cancelled) return StageInspectResult::Cancelled;
  if (checked == DigestResult::WrongSize) return StageInspectResult::WrongSize;
  if (checked != DigestResult::Ok) return StageInspectResult::Unavailable;
  memcpy(digest, result, sizeof(result));
  return StageInspectResult::Complete;
}
}  // namespace syncstage
