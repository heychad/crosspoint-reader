#include "RecoveryPreview.h"

#include <Memory.h>

#include <cstdio>
#include <cstring>

namespace syncpublish {
PreviewResult previewRecovery(HalStorage& storage, RecoveryPreview& output, syncstage::DigestTick tick, void* context) {
  if (!tick) return PreviewResult::Unavailable;
  HalStorage::MutationSnapshot snapshot;
  if (!storage.beginMutationSnapshot(snapshot)) return PreviewResult::Unavailable;
  if (!tick(context)) return PreviewResult::Cancelled;
  auto record = makeUniqueNoThrow<Record>();
  auto candidate = makeUniqueNoThrow<RecoveryPreview>();
  if (!record || !candidate) return PreviewResult::Unavailable;
  size_t length = 0;
  const auto inspected = inspectIntent(storage, *record, length);
  if (!storage.mutationSnapshotUnchanged(snapshot)) return PreviewResult::Changed;
  if (inspected == InspectResult::Missing) return PreviewResult::NoIntent;
  if (inspected == InspectResult::Damaged) return PreviewResult::Damaged;
  if (inspected != InspectResult::Valid) return PreviewResult::Unavailable;
  IntentView intent{};
  if (!decode(record->data(), length, intent)) return PreviewResult::Damaged;
  memcpy(candidate->stage.data(), intent.stage.data(), intent.stage.size());
  memcpy(candidate->destination.data(), intent.destination.data(), intent.destination.size());
  candidate->size = intent.size;
  char stagePath[STAGE_BYTES + 2] = {'/'};
  memcpy(stagePath + 1, intent.stage.data(), intent.stage.size());
  const auto stage = storage.lookupPath(stagePath);
  const auto destination = storage.lookupPath(candidate->destination.data());
  if (!storage.mutationSnapshotUnchanged(snapshot)) return PreviewResult::Changed;
  if (stage == HalStorage::PathStatus::Failed || destination == HalStorage::PathStatus::Failed)
    return PreviewResult::Unavailable;
  if (destination == HalStorage::PathStatus::Present) {
    uint8_t digest[32];
    const auto result =
        syncstage::hashDestination(storage, candidate->destination.data(), candidate->size, digest, tick, context);
    if (!storage.mutationSnapshotUnchanged(snapshot)) return PreviewResult::Changed;
    if (result == syncstage::DigestResult::Cancelled) return PreviewResult::Cancelled;
    if (result == syncstage::DigestResult::Changed) return PreviewResult::Changed;
    if (result == syncstage::DigestResult::WrongSize)
      candidate->destinationState = DestinationState::Mismatch;
    else if (result != syncstage::DigestResult::Ok)
      return PreviewResult::Unavailable;
    else
      candidate->destinationState = memcmp(digest, intent.digest.data(), sizeof(digest)) == 0
                                        ? DestinationState::Matches
                                        : DestinationState::Mismatch;
  }
  if (stage == HalStorage::PathStatus::Present) {
    uint8_t digest[32];
    const auto result = syncstage::hashStage(storage, candidate->stage.data(), candidate->size, digest, tick, context);
    if (!storage.mutationSnapshotUnchanged(snapshot)) return PreviewResult::Changed;
    if (result == syncstage::DigestResult::Cancelled) return PreviewResult::Cancelled;
    if (result == syncstage::DigestResult::Changed) return PreviewResult::Changed;
    if (result == syncstage::DigestResult::WrongSize)
      candidate->stageState = StageState::Mismatch;
    else if (result != syncstage::DigestResult::Ok)
      return PreviewResult::Unavailable;
    else
      candidate->stageState =
          memcmp(digest, intent.digest.data(), sizeof(digest)) == 0 ? StageState::Matches : StageState::Mismatch;
  }
  for (unsigned slot = 0; slot < syncstage::PRIVATE_COPY_SLOTS; ++slot) {
    char privatePath[64];
    if (!syncstage::privateStagePath(candidate->stage.data(), slot, privatePath)) return PreviewResult::Unavailable;
    const auto copy = storage.lookupPath(privatePath);
    if (!storage.mutationSnapshotUnchanged(snapshot)) return PreviewResult::Changed;
    if (copy == HalStorage::PathStatus::Failed) return PreviewResult::Unavailable;
    if (copy == HalStorage::PathStatus::Missing) continue;
    uint8_t digest[32];
    const auto result =
        syncstage::hashPrivateStageSlot(storage, candidate->stage.data(), slot, candidate->size, digest, tick, context);
    if (!storage.mutationSnapshotUnchanged(snapshot)) return PreviewResult::Changed;
    if (result == syncstage::DigestResult::Cancelled) return PreviewResult::Cancelled;
    if (result == syncstage::DigestResult::Changed) return PreviewResult::Changed;
    if (result != syncstage::DigestResult::Ok && result != syncstage::DigestResult::WrongSize)
      return PreviewResult::Unavailable;
    if (result == syncstage::DigestResult::Ok && memcmp(digest, intent.digest.data(), sizeof(digest)) == 0) {
      candidate->copyState = StageState::Matches;
      candidate->copySlot = static_cast<uint8_t>(slot);
      break;
    }
    candidate->copyState = StageState::Mismatch;
  }
  if (!tick(context)) return PreviewResult::Cancelled;
  if (!storage.mutationSnapshotUnchanged(snapshot)) return PreviewResult::Changed;
  output = *candidate;
  return PreviewResult::Observed;
}
}  // namespace syncpublish
