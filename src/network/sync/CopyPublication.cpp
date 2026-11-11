#include "CopyPublication.h"

#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace syncpublish {
namespace {
constexpr size_t BUFFER_BYTES = 4096;
struct CopyWork {
  HalStorage& storage;
  const IntentView& intent;
  HalStorage::MutationSnapshot before{};
  HalStorage::MutationSnapshot copied{};
  HalStorage::MutationMarker marker{};
  HalFile input;
  HalFile output;
  Record record{};
  char source[STAGE_BYTES + 2] = {'/'};
  char privatePath[64]{};
  uint8_t buffer[BUFFER_BYTES]{};
  uint64_t remaining = 0;
  CopyResult result = CopyResult::Unavailable;
};
bool openCopy(void* raw) {
  auto& work = *static_cast<CopyWork*>(raw);
  if (!work.storage.mutationSnapshotUnchanged(work.before)) {
    work.result = CopyResult::Changed;
    return true;
  }
  size_t length = 0;
  const auto status = inspectIntent(work.storage, work.record, length);
  if (!work.storage.mutationSnapshotUnchanged(work.before)) {
    work.result = CopyResult::Changed;
    return true;
  }
  if (status == InspectResult::Missing) {
    work.result = CopyResult::NoIntent;
    return true;
  }
  if (status != InspectResult::Valid) return true;
  IntentView saved{};
  if (!decode(work.record.data(), length, saved) || saved.stage != work.intent.stage ||
      saved.destination != work.intent.destination || saved.size != work.intent.size ||
      saved.digest != work.intent.digest) {
    work.result = CopyResult::Conflict;
    return true;
  }
  const auto stage = work.storage.lookupPath(work.source);
  const auto copy = work.storage.lookupPath(work.privatePath);
  if (!work.storage.mutationSnapshotUnchanged(work.before)) {
    work.result = CopyResult::Changed;
    return true;
  }
  if (copy == HalStorage::PathStatus::Present) {
    work.result = CopyResult::ExistingCopy;
    return true;
  }
  if (stage != HalStorage::PathStatus::Present || copy != HalStorage::PathStatus::Missing) return true;
  work.input = work.storage.openChecked(work.source, O_RDONLY);
  if (!work.input || work.input.isDirectory() || work.input.hasError() || work.input.fileSize64() != work.intent.size ||
      !work.storage.mutationSnapshotUnchanged(work.before))
    return true;
  work.result = CopyResult::Uncertain;
  work.output = work.storage.openChecked(work.privatePath, O_CREAT | O_EXCL | O_WRONLY);
  if (!work.output || !work.storage.observedSdmmcHealthy() || !work.storage.captureMutationMarker(work.marker))
    return true;
  work.remaining = work.intent.size;
  work.result = CopyResult::Copied;  // Internal: opened; final verification still required.
  return true;
}
bool copyChunk(void* raw) {
  auto& work = *static_cast<CopyWork*>(raw);
  if (!work.storage.mutationMarkerUnchanged(work.marker)) {
    work.result = CopyResult::Changed;
    return true;
  }
  const size_t count = static_cast<size_t>(std::min<uint64_t>(work.remaining, BUFFER_BYTES));
  if (work.input.read(work.buffer, count) != static_cast<int>(count) || work.input.hasError() ||
      work.output.write(work.buffer, count) != count || work.output.hasError() ||
      !work.storage.observedSdmmcHealthy() || !work.storage.captureMutationMarker(work.marker)) {
    work.result = CopyResult::Uncertain;
    return true;
  }
  work.remaining -= count;
  return true;
}
bool finishCopy(void* raw) {
  auto& work = *static_cast<CopyWork*>(raw);
  if (!work.storage.mutationMarkerUnchanged(work.marker)) {
    work.result = CopyResult::Changed;
    return true;
  }
  const bool synced = work.output.sync();
  const bool outputClosed = work.output.close();
  const bool inputClosed = work.input.close();
  if (!synced || !outputClosed || !inputClosed || !work.storage.beginMutationSnapshot(work.copied))
    work.result = CopyResult::Uncertain;
  return true;
}
bool closeCopy(void* raw) {
  auto& work = *static_cast<CopyWork*>(raw);
  if (work.output) work.output.close();
  if (work.input) work.input.close();
  return true;
}
}  // namespace
CopyResult copyVerifiedStage(HalStorage& storage, const IntentView& intent, syncstage::DigestTick tick, void* context) {
  if (!valid(intent) || !tick) return CopyResult::Invalid;
  auto work = makeUniqueNoThrow<CopyWork>(storage, intent);
  if (!work || !storage.beginMutationSnapshot(work->before)) return CopyResult::Unavailable;
  memcpy(work->source + 1, intent.stage.data(), intent.stage.size());
  uint8_t digest[32];
  const auto initial = syncstage::hashStage(storage, work->source + 1, intent.size, digest, tick, context);
  if (!storage.mutationSnapshotUnchanged(work->before)) return CopyResult::Changed;
  if (initial == syncstage::DigestResult::Cancelled) return CopyResult::Cancelled;
  if (initial == syncstage::DigestResult::Changed) return CopyResult::Changed;
  if (initial == syncstage::DigestResult::WrongSize) return CopyResult::Conflict;
  if (initial != syncstage::DigestResult::Ok) return CopyResult::Unavailable;
  if (memcmp(digest, intent.digest.data(), sizeof(digest)) != 0) return CopyResult::Conflict;
  if (!tick(context)) return CopyResult::Cancelled;
  unsigned selected = 0;
  for (; selected < syncstage::PRIVATE_COPY_SLOTS; ++selected) {
    if (!syncstage::privateStagePath(work->source + 1, selected, work->privatePath)) return CopyResult::Invalid;
    if (!storage.withStorageLock(openCopy, work.get())) return CopyResult::Unavailable;
    if (work->result != CopyResult::ExistingCopy) break;
    const auto existing =
        syncstage::hashPrivateStageSlot(storage, work->source + 1, selected, intent.size, digest, tick, context);
    if (!storage.mutationSnapshotUnchanged(work->before)) return CopyResult::Changed;
    if (existing == syncstage::DigestResult::Cancelled) return CopyResult::Cancelled;
    if (existing == syncstage::DigestResult::Changed) return CopyResult::Changed;
    if (existing != syncstage::DigestResult::Ok && existing != syncstage::DigestResult::WrongSize)
      return CopyResult::Unavailable;
    if (existing == syncstage::DigestResult::Ok && memcmp(digest, intent.digest.data(), sizeof(digest)) == 0)
      return CopyResult::VerifiedExistingCopy;
  }
  if (selected == syncstage::PRIVATE_COPY_SLOTS) return CopyResult::ExistingCopy;
  if (work->result != CopyResult::Copied) {
    storage.withStorageLock(closeCopy, work.get());
    return work->result;
  }
  while (work->remaining && work->result == CopyResult::Copied) {
    if (!tick(context)) {
      work->result = CopyResult::Cancelled;
      break;
    }
    if (!storage.withStorageLock(copyChunk, work.get())) {
      work->result = CopyResult::Uncertain;
      break;
    }
  }
  if (work->result == CopyResult::Copied && !storage.withStorageLock(finishCopy, work.get()))
    work->result = CopyResult::Uncertain;
  if (work->result != CopyResult::Copied) {
    storage.withStorageLock(closeCopy, work.get());
    if (work->result == CopyResult::Changed) return CopyResult::ChangedPartial;
    if (work->result == CopyResult::Cancelled) return CopyResult::CancelledPartial;
    return work->result;
  }
  const auto checked =
      syncstage::hashPrivateStageSlot(storage, work->source + 1, selected, intent.size, digest, tick, context);
  if (!storage.mutationSnapshotUnchanged(work->copied)) return CopyResult::ChangedPartial;
  if (checked == syncstage::DigestResult::Cancelled) return CopyResult::CancelledPartial;
  if (checked != syncstage::DigestResult::Ok || memcmp(digest, intent.digest.data(), sizeof(digest)) != 0)
    return CopyResult::Uncertain;
  return CopyResult::Copied;
}
}  // namespace syncpublish
