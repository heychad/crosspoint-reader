#include "EnsureParents.h"

#include <Memory.h>
#include <network/sync/PathDirectoryScan.h>

#include <cstring>

#include "PublishIntent.h"

namespace syncpublish {
namespace {
struct ScanContext {
  syncstage::DigestTick tick;
  void* context;
  const char* component;
  bool found = false, conflict = false, cancelled = false, exceeded = false;
  size_t entries = 0;
};
bool keepGoing(void* raw) {
  auto& scan = *static_cast<ScanContext*>(raw);
  if (scan.tick(scan.context)) return true;
  scan.cancelled = true;
  return false;
}
bool emit(void* raw, const wirecheck::DirectoryEntry& entry) {
  auto& scan = *static_cast<ScanContext*>(raw);
  if (++scan.entries > 4096) {
    scan.exceeded = true;
    return false;
  }
  if (std::string_view(entry.name) == scan.component) {
    if (scan.found || !entry.directory) {
      scan.conflict = true;
      return false;
    }
    scan.found = true;
  }
  return true;
}
struct ParentWork {
  HalStorage& storage;
  HalStorage::MutationSnapshot snapshot{};
  char parent[MAX_DESTINATION + 1] = {'/', 0};
  char candidate[MAX_DESTINATION + 1]{};
  char component[766]{};
  ParentState state = ParentState::Unavailable;
};
bool createLocked(void* raw) {
  auto& work = *static_cast<ParentWork*>(raw);
  if (!work.storage.mutationSnapshotUnchanged(work.snapshot)) {
    work.state = ParentState::Changed;
    return true;
  }
  const auto status = work.storage.lookupPath(work.candidate);
  if (!work.storage.mutationSnapshotUnchanged(work.snapshot)) {
    work.state = ParentState::Changed;
    return true;
  }
  if (status == HalStorage::PathStatus::Present) {
    work.state = ParentState::Conflict;
    return true;
  }
  if (status != HalStorage::PathStatus::Missing) return true;
  if (!work.storage.mkdir(work.candidate, false) || !work.storage.beginMutationSnapshot(work.snapshot)) {
    work.state = ParentState::Uncertain;
    return true;
  }
  auto directory = work.storage.openChecked(work.candidate, O_RDONLY);
  if (!directory) {
    work.state = ParentState::Uncertain;
    return true;
  }
  const bool isDirectory = directory.isDirectory() && !directory.hasError();
  const bool closed = directory.close();
  if (!isDirectory || !closed || !work.storage.mutationSnapshotUnchanged(work.snapshot)) {
    work.state = ParentState::Uncertain;
    return true;
  }
  work.state = ParentState::Ready;
  return true;
}
}  // namespace
ParentResult ensureParents(HalStorage& storage, std::string_view destination, syncstage::DigestTick tick,
                           void* context) {
  if (!validDestination(destination) || !tick) return {ParentState::Invalid, 0};
  auto work = makeUniqueNoThrow<ParentWork>(storage);
  if (!work || !storage.beginMutationSnapshot(work->snapshot)) return {ParentState::Unavailable, 0};
  unsigned created = 0;
  const size_t leaf = destination.find_last_of('/');
  size_t parentLength = 1;
  for (size_t start = 1; start < leaf;) {
    const auto slash = destination.find('/', start);
    const size_t length = slash - start;
    if (!length || length >= sizeof(work->component)) return {ParentState::Invalid, created};
    memcpy(work->component, destination.data() + start, length);
    work->component[length] = 0;
    ScanContext scan{tick, context, work->component};
    auto scanner = makeUniqueNoThrow<wirecheck::PathDirectoryScan>(
        storage, work->snapshot, work->parent, wirecheck::PathDirectoryScan::Callbacks{&scan, keepGoing, emit});
    if (!scanner) return {ParentState::Unavailable, created};
    auto result = wirecheck::ScanResult::Pending;
    while (result == wirecheck::ScanResult::Pending) result = scanner->step();
    scanner.reset();
    if (!storage.mutationSnapshotUnchanged(work->snapshot)) return {ParentState::Changed, created};
    if (scan.cancelled) return {ParentState::Cancelled, created};
    if (scan.conflict) return {ParentState::Conflict, created};
    if (scan.exceeded || result != wirecheck::ScanResult::Complete) return {ParentState::Unavailable, created};
    memcpy(work->candidate, work->parent, parentLength);
    size_t next = parentLength;
    if (next > 1) work->candidate[next++] = '/';
    memcpy(work->candidate + next, work->component, length);
    next += length;
    work->candidate[next] = 0;
    if (!scan.found) {
      work->state = ParentState::Unavailable;
      if (!storage.withStorageLock(createLocked, work.get())) return {ParentState::Unavailable, created};
      if (work->state != ParentState::Ready) return {work->state, created};
      ++created;
    }
    memcpy(work->parent, work->candidate, next + 1);
    parentLength = next;
    start = slash + 1;
  }
  return storage.mutationSnapshotUnchanged(work->snapshot) ? ParentResult{ParentState::Ready, created}
                                                           : ParentResult{ParentState::Changed, created};
}
}  // namespace syncpublish
