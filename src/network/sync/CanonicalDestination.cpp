#include "CanonicalDestination.h"

#include <Memory.h>
#include <network/sync/PathDirectoryScan.h>
#include <strings.h>

#include <cstring>

#include "PublishIntent.h"
#include "StageName.h"

namespace syncpublish {
namespace {
struct ScanContext {
  syncstage::DigestTick tick;
  void* context;
  const char* wanted;
  bool finalDirectory;
  bool root;
  bool found = false;
  bool conflict = false;
  bool cancelled = false;
  bool failed = false;
  size_t entries = 0;
};
bool keepGoing(void* raw) {
  auto& state = *static_cast<ScanContext*>(raw);
  if (state.tick(state.context)) return true;
  state.cancelled = true;
  return false;
}
bool emit(void* raw, const wirecheck::DirectoryEntry& entry) {
  auto& state = *static_cast<ScanContext*>(raw);
  if (++state.entries > 4096) {
    state.failed = true;
    return false;
  }
  const std::string_view name(entry.name);
  if (!state.finalDirectory) {
    if (name == state.wanted) {
      if (state.found || !entry.directory) {
        state.conflict = true;
        return false;
      }
      state.found = true;
    }
    return true;
  }
  if (state.root && syncstage::validName(name)) return true;
  for (const char* suffix : {".part", ".tmp", ".new", ".bak"}) {
    const size_t length = strlen(suffix);
    if (name.size() >= length && strncasecmp(name.data() + name.size() - length, suffix, length) == 0) {
      state.conflict = true;
      return false;
    }
  }
  return true;
}
struct CanonicalWork {
  char directory[MAX_DESTINATION + 1] = {'/', 0};
  char component[766]{};
};
CanonicalResult scan(HalStorage& storage, const HalStorage::MutationSnapshot& snapshot, const char* directory,
                     ScanContext& context) {
  auto scanner = makeUniqueNoThrow<wirecheck::PathDirectoryScan>(
      storage, snapshot, directory, wirecheck::PathDirectoryScan::Callbacks{&context, keepGoing, emit});
  if (!scanner) return CanonicalResult::Unavailable;
  auto status = wirecheck::ScanResult::Pending;
  while (status == wirecheck::ScanResult::Pending) status = scanner->step();
  if (!storage.mutationSnapshotUnchanged(snapshot)) return CanonicalResult::Changed;
  if (context.cancelled) return CanonicalResult::Cancelled;
  if (context.conflict) return CanonicalResult::Conflict;
  if (context.failed || status != wirecheck::ScanResult::Complete) return CanonicalResult::Unavailable;
  return CanonicalResult::Clear;
}
}  // namespace
CanonicalResult inspectCanonicalDestination(HalStorage& storage, std::string_view destination,
                                            syncstage::DigestTick tick, void* context) {
  if (!validDestination(destination) || !tick) return CanonicalResult::Invalid;
  auto work = makeUniqueNoThrow<CanonicalWork>();
  if (!work) return CanonicalResult::Unavailable;
  HalStorage::MutationSnapshot snapshot;
  if (!storage.beginMutationSnapshot(snapshot)) return CanonicalResult::Unavailable;
  const size_t leaf = destination.find_last_of('/');
  size_t directoryLength = 1;
  for (size_t start = 1; start < leaf;) {
    const size_t slash = destination.find('/', start);
    const size_t length = slash - start;
    if (length == 0 || length >= sizeof(work->component)) return CanonicalResult::Invalid;
    memcpy(work->component, destination.data() + start, length);
    work->component[length] = 0;
    ScanContext current{tick, context, work->component, false, directoryLength == 1};
    const auto result = scan(storage, snapshot, work->directory, current);
    if (result != CanonicalResult::Clear) return result;
    if (directoryLength > 1) work->directory[directoryLength++] = '/';
    memcpy(work->directory + directoryLength, work->component, length);
    directoryLength += length;
    work->directory[directoryLength] = 0;
    if (!current.found) {
      const auto path = storage.lookupPath(work->directory);
      if (!storage.mutationSnapshotUnchanged(snapshot)) return CanonicalResult::Changed;
      return path == HalStorage::PathStatus::Missing   ? CanonicalResult::MissingParent
             : path == HalStorage::PathStatus::Present ? CanonicalResult::NonCanonical
                                                       : CanonicalResult::Unavailable;
    }
    start = slash + 1;
  }
  ScanContext final{tick, context, nullptr, true, directoryLength == 1};
  return scan(storage, snapshot, work->directory, final);
}
}  // namespace syncpublish
