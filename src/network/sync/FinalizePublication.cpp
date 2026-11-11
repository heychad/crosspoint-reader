#include "FinalizePublication.h"

#include <Memory.h>

#include <cstring>

#include "CanonicalDestination.h"
#include "DestinationGuard.h"
#include "PublishIntentStore.h"
#include "RecoveryPreview.h"
#include "StoredReferenceGuard.h"

namespace syncpublish {
namespace {
struct Work {
  HalStorage& storage;
  HalStorage::MutationSnapshot before{};
  HalStorage::MutationSnapshot after{};
  Record expected{};
  size_t length = 0;
  char source[64]{};
  char destination[MAX_DESTINATION + 1]{};
  PublicationGate gate;
  void* gateContext;
  FinalizeResult result = FinalizeResult::Unavailable;
};
bool finalizeLocked(void* raw) {
  auto& work = *static_cast<Work*>(raw);
  if (!work.storage.mutationSnapshotUnchanged(work.before)) {
    work.result = FinalizeResult::Changed;
    return true;
  }
  auto actual = makeUniqueNoThrow<Record>();
  if (!actual) return true;
  size_t length = 0;
  if (inspectIntent(work.storage, *actual, length) != InspectResult::Valid || length != work.length ||
      memcmp(actual->data(), work.expected.data(), length) != 0) {
    work.result = FinalizeResult::Conflict;
    return true;
  }
  const auto source = work.storage.lookupPath(work.source);
  const auto dest = work.storage.lookupPath(work.destination);
  if (!work.storage.mutationSnapshotUnchanged(work.before)) {
    work.result = FinalizeResult::Changed;
    return true;
  }
  if (source == HalStorage::PathStatus::Failed || dest == HalStorage::PathStatus::Failed) return true;
  if (source != HalStorage::PathStatus::Present || dest != HalStorage::PathStatus::Missing ||
      !work.gate(work.destination, work.gateContext)) {
    work.result = FinalizeResult::Conflict;
    return true;
  }
  if (!work.storage.mutationSnapshotUnchanged(work.before)) {
    work.result = FinalizeResult::Changed;
    return true;
  }
  if (!work.storage.rename(work.source, work.destination) || !work.storage.beginMutationSnapshot(work.after)) {
    work.result = FinalizeResult::Uncertain;
    return true;
  }
  work.result = FinalizeResult::Published;  // Hash and path checks follow.
  return true;
}
}  // namespace
FinalizeResult finalizeVerifiedCopy(HalStorage& storage, syncstage::DigestTick tick, void* tickContext,
                                    PublicationGate gate, void* gateContext, PublicationAcquire acquire,
                                    PublicationRelease release, void* lockContext) {
  if (!tick || !gate || !acquire || !release) return FinalizeResult::Unavailable;
  auto work = makeUniqueNoThrow<Work>(storage);
  auto preview = makeUniqueNoThrow<RecoveryPreview>();
  if (!work || !preview || !storage.beginMutationSnapshot(work->before)) return FinalizeResult::Unavailable;
  work->gate = gate;
  work->gateContext = gateContext;
  const auto observed = previewRecovery(storage, *preview, tick, tickContext);
  if (!storage.mutationSnapshotUnchanged(work->before)) return FinalizeResult::Changed;
  if (observed == PreviewResult::NoIntent) return FinalizeResult::NoIntent;
  if (observed == PreviewResult::Cancelled) return FinalizeResult::Cancelled;
  if (observed == PreviewResult::Changed) return FinalizeResult::Changed;
  if (observed != PreviewResult::Observed) return FinalizeResult::Unavailable;
  // An equal destination is only an observation, not proof that this intent
  // created it. Leave the intent and all files untouched for reconciliation.
  if (preview->stageState == StageState::Matches && preview->destinationState == DestinationState::Matches)
    return FinalizeResult::MatchingDestination;
  if (preview->stageState != StageState::Matches || preview->copyState != StageState::Matches ||
      preview->destinationState != DestinationState::Missing || preview->copySlot == RecoveryPreview::NO_COPY_SLOT)
    return FinalizeResult::Conflict;
  if (inspectIntent(storage, work->expected, work->length) != InspectResult::Valid) return FinalizeResult::Unavailable;
  IntentView intent{};
  if (!decode(work->expected.data(), work->length, intent)) return FinalizeResult::Unavailable;
  if (!syncstage::privateStagePath(preview->stage.data(), preview->copySlot, work->source))
    return FinalizeResult::Unavailable;
  memcpy(work->destination, intent.destination.data(), intent.destination.size());
  const auto canonical = inspectCanonicalDestination(storage, intent.destination, tick, tickContext);
  if (!storage.mutationSnapshotUnchanged(work->before) || canonical == CanonicalResult::Changed)
    return FinalizeResult::Changed;
  if (canonical == CanonicalResult::Cancelled) return FinalizeResult::Cancelled;
  if (canonical != CanonicalResult::Clear) return FinalizeResult::Conflict;
  const auto references = inspectStoredReferences(storage, intent.destination, tick, tickContext);
  if (!storage.mutationSnapshotUnchanged(work->before) || references == ReferenceGuardResult::Changed)
    return FinalizeResult::Changed;
  if (references == ReferenceGuardResult::Cancelled) return FinalizeResult::Cancelled;
  if (references != ReferenceGuardResult::Clear) return FinalizeResult::Conflict;
  const auto destination = inspectDestinationState(storage, intent.destination, tick, tickContext);
  if (!storage.mutationSnapshotUnchanged(work->before) || destination == DestinationGuardResult::Changed)
    return FinalizeResult::Changed;
  if (destination == DestinationGuardResult::Cancelled) return FinalizeResult::Cancelled;
  if (destination != DestinationGuardResult::Clear) return FinalizeResult::Conflict;
  if (!tick(tickContext)) return FinalizeResult::Cancelled;
  if (!acquire(lockContext)) return FinalizeResult::Unavailable;
  const bool locked = storage.withStorageLock(finalizeLocked, work.get());
  release(lockContext);
  if (!locked) return FinalizeResult::Unavailable;
  if (work->result != FinalizeResult::Published) return work->result;
  uint8_t digest[32];
  const auto hash = syncstage::hashDestination(storage, work->destination, intent.size, digest, tick, tickContext);
  if (hash != syncstage::DigestResult::Ok || memcmp(digest, intent.digest.data(), sizeof(digest)) != 0 ||
      !storage.mutationSnapshotUnchanged(work->after) ||
      storage.lookupPath(work->source) != HalStorage::PathStatus::Missing ||
      !storage.mutationSnapshotUnchanged(work->after))
    return FinalizeResult::Uncertain;
  return FinalizeResult::Published;
}
}  // namespace syncpublish
