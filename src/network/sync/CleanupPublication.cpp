#include "CleanupPublication.h"

#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "PublishIntentStore.h"

namespace syncpublish {
namespace {
struct CleanupWork {
  HalStorage& storage;
  HalStorage::MutationSnapshot before{};
  HalStorage::MutationSnapshot after{};
  Record record{};
  char archive[64]{};
  char source[STAGE_BYTES + 2] = {'/'};
  char destination[MAX_DESTINATION + 1]{};
  CleanupResult result = CleanupResult::Unavailable;
};
bool removeLocked(void* raw) {
  auto& work = *static_cast<CleanupWork*>(raw);
  if (!work.storage.mutationSnapshotUnchanged(work.before)) {
    work.result = CleanupResult::Changed;
    return true;
  }
  const auto pending = work.storage.lookupPath(INTENT_PATH);
  const auto stage = work.storage.lookupPath(work.source);
  const auto archive = work.storage.lookupPath(work.archive);
  const auto destination = work.storage.lookupPath(work.destination);
  if (!work.storage.mutationSnapshotUnchanged(work.before)) {
    work.result = CleanupResult::Changed;
    return true;
  }
  if (pending == HalStorage::PathStatus::Failed || stage == HalStorage::PathStatus::Failed ||
      archive == HalStorage::PathStatus::Failed || destination == HalStorage::PathStatus::Failed)
    return true;
  if (pending != HalStorage::PathStatus::Missing || stage != HalStorage::PathStatus::Present ||
      archive != HalStorage::PathStatus::Present || destination != HalStorage::PathStatus::Present) {
    work.result = CleanupResult::Conflict;
    return true;
  }
  if (!work.storage.remove(work.source) || !work.storage.beginMutationSnapshot(work.after)) {
    work.result = CleanupResult::Uncertain;
    return true;
  }
  work.result = CleanupResult::Cleaned;
  return true;
}
}  // namespace
CleanupResult cleanupArchivedStage(HalStorage& storage, const char* stage, syncstage::DigestTick tick, void* context) {
  if (!stage || !syncstage::validName(stage) || !tick) return CleanupResult::Invalid;
  auto work = makeUniqueNoThrow<CleanupWork>(storage);
  if (!work || !storage.beginMutationSnapshot(work->before)) return CleanupResult::Unavailable;
  memcpy(work->source + 1, stage, STAGE_BYTES);
  const int written = snprintf(work->archive, sizeof(work->archive), "/.crosspoint/xsync-%.32s.done", stage + 8);
  if (written < 0 || static_cast<size_t>(written) >= sizeof(work->archive)) return CleanupResult::Invalid;
  if (storage.lookupPath(INTENT_PATH) != HalStorage::PathStatus::Missing) return CleanupResult::Conflict;
  auto file = storage.openChecked(work->archive, O_RDONLY);
  if (!file) return CleanupResult::Unavailable;
  const uint64_t length = file.fileSize64();
  const bool read = !file.isDirectory() && length >= HEADER_BYTES + 5 && length <= MAX_RECORD &&
                    file.read(work->record.data(), static_cast<size_t>(length)) == static_cast<int>(length) &&
                    !file.hasError();
  const bool closed = file.close();
  if (!storage.mutationSnapshotUnchanged(work->before)) return CleanupResult::Changed;
  if (!read || !closed) return CleanupResult::Unavailable;
  IntentView intent{};
  if (!decode(work->record.data(), static_cast<size_t>(length), intent) || intent.stage != stage)
    return CleanupResult::Conflict;
  memcpy(work->destination, intent.destination.data(), intent.destination.size());
  uint8_t digest[32];
  const auto destination = syncstage::hashDestination(storage, work->destination, intent.size, digest, tick, context);
  if (!storage.mutationSnapshotUnchanged(work->before) || destination == syncstage::DigestResult::Changed)
    return CleanupResult::Changed;
  if (destination == syncstage::DigestResult::Cancelled) return CleanupResult::Cancelled;
  if (destination != syncstage::DigestResult::Ok || memcmp(digest, intent.digest.data(), sizeof(digest)) != 0)
    return CleanupResult::Conflict;
  const auto status = storage.lookupPath(work->source);
  if (!storage.mutationSnapshotUnchanged(work->before)) return CleanupResult::Changed;
  if (status == HalStorage::PathStatus::Missing) return CleanupResult::AlreadyClean;
  if (status != HalStorage::PathStatus::Present) return CleanupResult::Unavailable;
  const auto source = syncstage::hashStage(storage, stage, intent.size, digest, tick, context);
  if (!storage.mutationSnapshotUnchanged(work->before) || source == syncstage::DigestResult::Changed)
    return CleanupResult::Changed;
  if (source == syncstage::DigestResult::Cancelled) return CleanupResult::Cancelled;
  if (source != syncstage::DigestResult::Ok || memcmp(digest, intent.digest.data(), sizeof(digest)) != 0)
    return CleanupResult::Conflict;
  if (!tick(context)) return CleanupResult::Cancelled;
  if (!storage.withStorageLock(removeLocked, work.get())) return CleanupResult::Unavailable;
  if (work->result != CleanupResult::Cleaned) return work->result;
  const auto verified = syncstage::hashDestination(storage, work->destination, intent.size, digest, tick, context);
  if (verified != syncstage::DigestResult::Ok || memcmp(digest, intent.digest.data(), sizeof(digest)) != 0 ||
      !storage.mutationSnapshotUnchanged(work->after) ||
      storage.lookupPath(work->source) != HalStorage::PathStatus::Missing ||
      !storage.mutationSnapshotUnchanged(work->after))
    return CleanupResult::Uncertain;
  return CleanupResult::Cleaned;
}
}  // namespace syncpublish
