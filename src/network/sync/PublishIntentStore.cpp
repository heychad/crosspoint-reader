#include "PublishIntentStore.h"

#include <Memory.h>

#include <algorithm>
#include <cstring>

namespace syncpublish {
namespace {
struct PrepareContext {
  HalStorage& storage;
  const Record& record;
  size_t size;
  PrepareResult result = PrepareResult::Unavailable;
};
bool prepareLocked(void* opaque) {
  auto& ctx = *static_cast<PrepareContext*>(opaque);
  HalStorage::MutationSnapshot snapshot;
  if (!ctx.storage.beginMutationSnapshot(snapshot)) return true;
  const auto parent = ctx.storage.lookupPath("/.crosspoint");
  if (parent != HalStorage::PathStatus::Present) return true;
  const auto current = ctx.storage.lookupPath(INTENT_PATH);
  if (current == HalStorage::PathStatus::Present) {
    ctx.result = PrepareResult::Existing;
    return true;
  }
  if (current != HalStorage::PathStatus::Missing || !ctx.storage.mutationSnapshotUnchanged(snapshot)) return true;
  ctx.result = PrepareResult::Uncertain;
  auto file = ctx.storage.openChecked(INTENT_PATH, O_CREAT | O_EXCL | O_WRONLY);
  if (!file) return true;
  const bool written = file.write(ctx.record.data(), ctx.size) == ctx.size && !file.hasError();
  const bool synced = file.sync();
  const bool closed = file.close();
  if (!written || !synced || !closed || !ctx.storage.observedSdmmcHealthy()) return true;
  auto check = ctx.storage.openChecked(INTENT_PATH, O_RDONLY);
  if (!check) return true;
  bool matches = !check.isDirectory() && !check.hasError() && check.fileSize64() == ctx.size;
  uint8_t chunk[128];
  for (size_t offset = 0; matches && offset < ctx.size;) {
    const size_t count = std::min(sizeof(chunk), ctx.size - offset);
    matches = check.read(chunk, count) == static_cast<int>(count) && !check.hasError() &&
              memcmp(chunk, ctx.record.data() + offset, count) == 0;
    offset += count;
  }
  const bool clean = !check.hasError();
  const bool checkClosed = check.close();
  if (matches && clean && checkClosed && ctx.storage.observedSdmmcHealthy()) ctx.result = PrepareResult::Prepared;
  return true;
}
struct InspectContext {
  HalStorage& storage;
  Record& record;
  size_t size = 0;
  InspectResult result = InspectResult::Unavailable;
};
bool inspectLocked(void* opaque) {
  auto& ctx = *static_cast<InspectContext*>(opaque);
  HalStorage::MutationSnapshot snapshot;
  if (!ctx.storage.beginMutationSnapshot(snapshot)) return true;
  const auto status = ctx.storage.lookupPath(INTENT_PATH);
  if (status == HalStorage::PathStatus::Missing) {
    if (ctx.storage.mutationSnapshotUnchanged(snapshot)) ctx.result = InspectResult::Missing;
    return true;
  }
  if (status != HalStorage::PathStatus::Present) return true;
  auto file = ctx.storage.openChecked(INTENT_PATH, O_RDONLY);
  if (!file) return true;
  const bool directory = file.isDirectory();
  const uint64_t length = file.fileSize64();
  bool read = false;
  if (!directory && length <= MAX_RECORD && length >= HEADER_BYTES + 5)
    read = file.read(ctx.record.data(), static_cast<size_t>(length)) == static_cast<int>(length) && !file.hasError();
  const bool clean = !file.hasError();
  const bool closed = file.close();
  if (!clean || !closed || !ctx.storage.mutationSnapshotUnchanged(snapshot)) return true;
  if (directory || length > MAX_RECORD || length < HEADER_BYTES + 5) {
    ctx.result = InspectResult::Damaged;
    return true;
  }
  if (!read) return true;
  IntentView decoded{};
  if (!decode(ctx.record.data(), static_cast<size_t>(length), decoded)) {
    ctx.result = InspectResult::Damaged;
    return true;
  }
  ctx.size = static_cast<size_t>(length);
  ctx.result = InspectResult::Valid;
  return true;
}
}  // namespace
PrepareResult prepareIntent(HalStorage& storage, const IntentView& intent) {
  if (!valid(intent)) return PrepareResult::Invalid;
  auto record = makeUniqueNoThrow<Record>();
  if (!record) return PrepareResult::Unavailable;
  size_t length = 0;
  if (!encode(intent, *record, length)) return PrepareResult::Invalid;
  PrepareContext context{storage, *record, length};
  return storage.withStorageLock(prepareLocked, &context) ? context.result : PrepareResult::Unavailable;
}
InspectResult inspectIntent(HalStorage& storage, Record& output, size_t& length) {
  auto record = makeUniqueNoThrow<Record>();
  if (!record) return InspectResult::Unavailable;
  InspectContext context{storage, *record};
  if (!storage.withStorageLock(inspectLocked, &context)) return InspectResult::Unavailable;
  if (context.result == InspectResult::Valid) {
    output = *record;
    length = context.size;
  }
  return context.result;
}
}  // namespace syncpublish
