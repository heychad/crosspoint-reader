#include "PreparePublication.h"

#include <Memory.h>

#include <cstring>

#include "CanonicalDestination.h"
#include "DestinationGuard.h"
#include "StoredReferenceGuard.h"

namespace syncpublish {
namespace {
struct Work {
  HalStorage& storage;
  const IntentView& intent;
  HalStorage::MutationSnapshot snapshot{};
  char stage[STAGE_BYTES + 2] = {'/'};
  char parent[MAX_DESTINATION + 1]{};
  char destination[MAX_DESTINATION + 1]{};
  PreflightResult result = PreflightResult::Unavailable;
};
bool prepareLocked(void* raw) {
  auto& work = *static_cast<Work*>(raw);
  if (!work.storage.mutationSnapshotUnchanged(work.snapshot)) {
    work.result = PreflightResult::Changed;
    return true;
  }
  const auto parent = work.storage.lookupPath(work.parent);
  const auto destination = work.storage.lookupPath(work.destination);
  const auto stage = work.storage.lookupPath(work.stage);
  if (!work.storage.mutationSnapshotUnchanged(work.snapshot)) {
    work.result = PreflightResult::Changed;
    return true;
  }
  if (parent == HalStorage::PathStatus::Failed || destination == HalStorage::PathStatus::Failed ||
      stage == HalStorage::PathStatus::Failed)
    return true;
  if (parent == HalStorage::PathStatus::Missing) {
    work.result = PreflightResult::ParentMissing;
    return true;
  }
  if (destination == HalStorage::PathStatus::Present) {
    work.result = PreflightResult::DestinationPresent;
    return true;
  }
  if (stage == HalStorage::PathStatus::Missing) {
    work.result = PreflightResult::StageMissing;
    return true;
  }
  auto dir = work.storage.openChecked(work.parent, O_RDONLY);
  if (!dir) return true;
  const bool directory = dir.isDirectory() && !dir.hasError();
  const bool closed = dir.close();
  if (!closed || !work.storage.mutationSnapshotUnchanged(work.snapshot)) {
    work.result = PreflightResult::Changed;
    return true;
  }
  if (!directory) return true;
  switch (prepareIntent(work.storage, work.intent)) {
    case PrepareResult::Prepared:
      work.result = PreflightResult::Prepared;
      break;
    case PrepareResult::Existing:
      work.result = PreflightResult::ExistingIntent;
      break;
    case PrepareResult::Invalid:
      work.result = PreflightResult::Invalid;
      break;
    case PrepareResult::Uncertain:
      work.result = PreflightResult::Uncertain;
      break;
    case PrepareResult::Unavailable:
      break;
  }
  return true;
}
}  // namespace
PreflightResult prepareVerifiedIntent(HalStorage& storage, const IntentView& intent, syncstage::DigestTick tick,
                                      void* context) {
  if (!valid(intent) || !tick) return PreflightResult::Invalid;
  auto work = makeUniqueNoThrow<Work>(storage, intent);
  if (!work || !storage.beginMutationSnapshot(work->snapshot)) return PreflightResult::Unavailable;
  memcpy(work->stage + 1, intent.stage.data(), intent.stage.size());
  memcpy(work->destination, intent.destination.data(), intent.destination.size());
  const auto slash = intent.destination.rfind('/');
  const auto parentLength = slash == 0 ? 1 : slash;
  memcpy(work->parent, intent.destination.data(), parentLength);
  const auto stage = storage.lookupPath(work->stage);
  if (!storage.mutationSnapshotUnchanged(work->snapshot)) return PreflightResult::Changed;
  if (stage == HalStorage::PathStatus::Missing) return PreflightResult::StageMissing;
  if (stage != HalStorage::PathStatus::Present) return PreflightResult::Unavailable;
  uint8_t digest[32];
  const auto checked = syncstage::hashStage(storage, work->stage + 1, intent.size, digest, tick, context);
  if (!storage.mutationSnapshotUnchanged(work->snapshot)) return PreflightResult::Changed;
  if (checked == syncstage::DigestResult::Cancelled) return PreflightResult::Cancelled;
  if (checked == syncstage::DigestResult::Changed) return PreflightResult::Changed;
  if (checked == syncstage::DigestResult::WrongSize) return PreflightResult::HashMismatch;
  if (checked != syncstage::DigestResult::Ok) return PreflightResult::Unavailable;
  if (memcmp(digest, intent.digest.data(), sizeof(digest)) != 0) return PreflightResult::HashMismatch;
  const auto canonical = inspectCanonicalDestination(storage, intent.destination, tick, context);
  if (!storage.mutationSnapshotUnchanged(work->snapshot)) return PreflightResult::Changed;
  if (canonical == CanonicalResult::MissingParent) return PreflightResult::ParentMissing;
  if (canonical == CanonicalResult::NonCanonical) return PreflightResult::NonCanonical;
  if (canonical == CanonicalResult::Conflict) return PreflightResult::ReservedState;
  if (canonical == CanonicalResult::Changed) return PreflightResult::Changed;
  if (canonical == CanonicalResult::Cancelled) return PreflightResult::Cancelled;
  if (canonical != CanonicalResult::Clear) return PreflightResult::Unavailable;
  const auto references = inspectStoredReferences(storage, intent.destination, tick, context);
  if (!storage.mutationSnapshotUnchanged(work->snapshot)) return PreflightResult::Changed;
  if (references == ReferenceGuardResult::Conflict) return PreflightResult::ReservedState;
  if (references == ReferenceGuardResult::Changed) return PreflightResult::Changed;
  if (references == ReferenceGuardResult::Cancelled) return PreflightResult::Cancelled;
  if (references != ReferenceGuardResult::Clear) return PreflightResult::Unavailable;
  const auto guard = inspectDestinationState(storage, intent.destination, tick, context);
  if (!storage.mutationSnapshotUnchanged(work->snapshot)) return PreflightResult::Changed;
  if (guard == DestinationGuardResult::Conflict) return PreflightResult::ReservedState;
  if (guard == DestinationGuardResult::Changed) return PreflightResult::Changed;
  if (guard == DestinationGuardResult::Cancelled) return PreflightResult::Cancelled;
  if (guard != DestinationGuardResult::Clear) return PreflightResult::Unavailable;
  if (!tick(context)) return PreflightResult::Cancelled;
  return storage.withStorageLock(prepareLocked, work.get()) ? work->result : PreflightResult::Unavailable;
}
}  // namespace syncpublish
