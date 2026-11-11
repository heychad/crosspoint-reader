#include "ArchivePublication.h"

#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "PublishIntentStore.h"
#include "RecoveryPreview.h"

namespace syncpublish {
namespace {
struct ArchiveWork {
  HalStorage& storage;
  HalStorage::MutationSnapshot before{};
  Record expected{};
  size_t length = 0;
  char path[64]{};
  char destination[MAX_DESTINATION + 1]{};
  ArchiveResult result = ArchiveResult::Unavailable;
};
bool archiveLocked(void* raw) {
  auto& work = *static_cast<ArchiveWork*>(raw);
  if (!work.storage.mutationSnapshotUnchanged(work.before)) {
    work.result = ArchiveResult::Changed;
    return true;
  }
  auto record = makeUniqueNoThrow<Record>();
  if (!record) return true;
  size_t length = 0;
  if (inspectIntent(work.storage, *record, length) != InspectResult::Valid || length != work.length ||
      memcmp(record->data(), work.expected.data(), length) != 0) {
    work.result = ArchiveResult::Conflict;
    return true;
  }
  const auto status = work.storage.lookupPath(work.path);
  if (!work.storage.mutationSnapshotUnchanged(work.before)) {
    work.result = ArchiveResult::Changed;
    return true;
  }
  if (status == HalStorage::PathStatus::Present) {
    work.result = ArchiveResult::Conflict;
    return true;
  }
  if (status != HalStorage::PathStatus::Missing) return true;
  if (!work.storage.rename(INTENT_PATH, work.path)) {
    work.result = ArchiveResult::Uncertain;
    return true;
  }
  work.result = ArchiveResult::Archived;
  return true;
}
ArchiveResult inspectArchive(ArchiveWork& work, const char* stage, syncstage::DigestTick tick, void* context) {
  const auto status = work.storage.lookupPath(work.path);
  if (!work.storage.mutationSnapshotUnchanged(work.before)) return ArchiveResult::Changed;
  if (status == HalStorage::PathStatus::Missing) return ArchiveResult::NoIntent;
  if (status != HalStorage::PathStatus::Present) return ArchiveResult::Unavailable;
  auto file = work.storage.openChecked(work.path, O_RDONLY);
  if (!file) return ArchiveResult::Unavailable;
  const auto length = file.fileSize64();
  if (file.isDirectory() || !length || length > MAX_RECORD) {
    file.close();
    return ArchiveResult::Conflict;
  }
  const bool read =
      file.read(work.expected.data(), static_cast<size_t>(length)) == static_cast<int>(length) && !file.hasError();
  const bool closed = file.close();
  if (!work.storage.mutationSnapshotUnchanged(work.before)) return ArchiveResult::Changed;
  if (!read || !closed) return ArchiveResult::Unavailable;
  IntentView intent{};
  if (!decode(work.expected.data(), static_cast<size_t>(length), intent) || intent.stage != stage)
    return ArchiveResult::Conflict;
  uint8_t digest[32];
  // The archived receipt and finished book remain verifiable even if cleanup
  // left the disposable source stage missing or damaged.
  memcpy(work.destination, intent.destination.data(), intent.destination.size());
  const auto dest = syncstage::hashDestination(work.storage, work.destination, intent.size, digest, tick, context);
  if (!work.storage.mutationSnapshotUnchanged(work.before) || dest == syncstage::DigestResult::Changed)
    return ArchiveResult::Changed;
  if (dest == syncstage::DigestResult::Cancelled) return ArchiveResult::Cancelled;
  if (dest == syncstage::DigestResult::WrongSize) return ArchiveResult::Conflict;
  if (dest != syncstage::DigestResult::Ok) return ArchiveResult::Unavailable;
  if (memcmp(digest, intent.digest.data(), sizeof(digest)) != 0) return ArchiveResult::Conflict;
  return ArchiveResult::AlreadyArchived;
}
}  // namespace
ArchiveResult archiveVerifiedIntent(HalStorage& storage, const char* stage, syncstage::DigestTick tick, void* context) {
  if (!stage || !syncstage::validName(stage) || !tick) return ArchiveResult::Unavailable;
  auto work = makeUniqueNoThrow<ArchiveWork>(storage);
  auto preview = makeUniqueNoThrow<RecoveryPreview>();
  if (!work || !preview || !storage.beginMutationSnapshot(work->before)) return ArchiveResult::Unavailable;
  const int written = snprintf(work->path, sizeof(work->path), "/.crosspoint/xsync-%.32s.done", stage + 8);
  if (written < 0 || static_cast<size_t>(written) >= sizeof(work->path)) return ArchiveResult::Unavailable;
  const auto observed = previewRecovery(storage, *preview, tick, context);
  if (!storage.mutationSnapshotUnchanged(work->before) || observed == PreviewResult::Changed)
    return ArchiveResult::Changed;
  if (observed == PreviewResult::NoIntent) return inspectArchive(*work, stage, tick, context);
  if (observed == PreviewResult::Cancelled) return ArchiveResult::Cancelled;
  if (observed != PreviewResult::Observed) return ArchiveResult::Unavailable;
  if (strcmp(preview->stage.data(), stage) != 0 || preview->stageState != StageState::Matches ||
      preview->destinationState != DestinationState::Matches)
    return ArchiveResult::Conflict;
  if (inspectIntent(storage, work->expected, work->length) != InspectResult::Valid) return ArchiveResult::Unavailable;
  IntentView intent{};
  if (!decode(work->expected.data(), work->length, intent) || intent.stage != stage) return ArchiveResult::Conflict;
  if (!tick(context)) return ArchiveResult::Cancelled;
  if (!storage.withStorageLock(archiveLocked, work.get())) return ArchiveResult::Unavailable;
  if (work->result != ArchiveResult::Archived) return work->result;
  // A successful rename is not sufficient to prove durability or ownership.
  // Read back both names and every byte before reporting the local operation.
  HalStorage::MutationSnapshot after;
  if (!storage.beginMutationSnapshot(after)) return ArchiveResult::Uncertain;
  if (storage.lookupPath(INTENT_PATH) != HalStorage::PathStatus::Missing) return ArchiveResult::Uncertain;
  auto file = storage.openChecked(work->path, O_RDONLY);
  if (!file) return ArchiveResult::Uncertain;
  auto actual = makeUniqueNoThrow<Record>();
  if (!actual) {
    file.close();
    return ArchiveResult::Uncertain;
  }
  const bool read = !file.isDirectory() && file.fileSize64() == work->length &&
                    file.read(actual->data(), work->length) == static_cast<int>(work->length) && !file.hasError();
  const bool closed = file.close();
  if (!read || !closed || memcmp(actual->data(), work->expected.data(), work->length) != 0 ||
      !storage.mutationSnapshotUnchanged(after))
    return ArchiveResult::Uncertain;
  return ArchiveResult::Archived;
}
}  // namespace syncpublish
