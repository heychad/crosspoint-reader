#include "DeferredCompletionIntentIO.h"

#include <HalStorage.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CompletionJournalIO.h"
#include "DeferredCompletionIntent.h"
#include "StatsSessionLease.h"

namespace readingstats::deferredcompletion {
namespace {
namespace cj = completionjournal;
constexpr char DIRECTORY[] = "/.completion-deferred";
void pathFor(unsigned slot, const char* prefix, char (&path)[64]) {
  snprintf(path, sizeof(path), "%s/%s%02u", DIRECTORY, prefix, slot);
}

enum class FileResult { Read, Missing, Invalid, Unavailable };
FileResult readFile(const char* path, uint8_t* bytes, size_t capacity, size_t& size) {
  const auto status = Storage.lookupPath(path);
  if (status == HalStorage::PathStatus::Missing) return FileResult::Missing;
  if (status != HalStorage::PathStatus::Present) return FileResult::Unavailable;
  HalFile file;
  if (!Storage.openFileForRead("STATS", path, file)) return FileResult::Unavailable;
  const size_t length = file.fileSize();
  if (length > capacity) return file.close() ? FileResult::Invalid : FileResult::Unavailable;
  const bool read = file.read(bytes, length) == static_cast<int>(length);
  if (!file.close() || !read) return FileResult::Unavailable;
  size = length;
  return FileResult::Read;
}

bool createFile(const char* path, const uint8_t* bytes, size_t size) {
  if (Storage.lookupPath(path) != HalStorage::PathStatus::Missing) return false;
  HalFile file = Storage.openChecked(path, O_WRONLY | O_CREAT | O_EXCL);
  if (!file) return false;
  const bool written = file.write(bytes, size) == size;
  const bool synced = file.sync();
  const bool closed = file.close();
  if (!written || !synced || !closed) return false;
  HalFile check;
  if (!Storage.openFileForRead("STATS", path, check)) return false;
  bool same = check.fileSize() == size;
  uint8_t buffer[64];
  for (size_t offset = 0; same && offset < size;) {
    const size_t count = std::min(sizeof(buffer), size - offset);
    same = check.read(buffer, count) == static_cast<int>(count) && std::memcmp(buffer, bytes + offset, count) == 0;
    offset += count;
  }
  return check.close() && same;
}

struct Context {
  std::string_view expectedSource;
  PublishResult result = PublishResult::Unavailable;
};

struct FindContext {
  std::string_view expectedSource;
  Record& output;
  size_t& size;
  unsigned& slot;
  FindResult result = FindResult::Unavailable;
  bool recoverRetired = false;
  uint64_t originHash = 0;
};

bool findLocked(void* raw) {
  auto& context = *static_cast<FindContext*>(raw);
  if (StatsSessionLease::anyActive()) {
    context.result = FindResult::Busy;
    return true;
  }
  const auto directoryStatus = Storage.lookupPath(DIRECTORY);
  if (directoryStatus == HalStorage::PathStatus::Missing) {
    context.result = FindResult::Missing;
    return true;
  }
  if (directoryStatus != HalStorage::PathStatus::Present) return true;
  uint64_t records = 0, seals = 0, retirements = 0;
  if (!Storage.lookupNumberedChildren(DIRECTORY, ".defer-", records) ||
      !Storage.lookupNumberedChildren(DIRECTORY, ".dseal-", seals) ||
      !Storage.lookupNumberedChildren(DIRECTORY, ".dretire-", retirements))
    return true;
  if ((seals & ~records) || (retirements & ~(records & seals))) {
    context.result = FindResult::Blocked;
    return true;
  }
  auto actual = makeUniqueNoThrow<Record>();
  if (!actual) return true;
  unsigned matching = 64;
  for (unsigned slot = 0; slot < 64; ++slot) {
    const uint64_t bit = uint64_t{1} << slot;
    if (!(records & bit)) continue;
    char path[64];
    pathFor(slot, ".defer-", path);
    size_t actualSize = 0;
    const auto read = readFile(path, actual->data(), actual->size(), actualSize);
    if (read == FileResult::Unavailable || read == FileResult::Missing) return true;
    uint64_t completionHash = 0;
    std::string_view source;
    if (read != FileResult::Read || !decodeWithHash(actual->data(), actualSize, completionHash, source)) {
      context.result = FindResult::Blocked;
      return true;
    }
    for (const bool retired : {false, true}) {
      if (!((retired ? retirements : seals) & bit)) continue;
      Seal expected{}, stored{};
      if (!encodeSeal(slot, *actual, actualSize, expected, retired)) return true;
      pathFor(slot, retired ? ".dretire-" : ".dseal-", path);
      size_t sealSize = 0;
      const auto sealRead = readFile(path, stored.data(), stored.size(), sealSize);
      if (sealRead == FileResult::Unavailable || sealRead == FileResult::Missing) return true;
      if (sealRead != FileResult::Read || sealSize != stored.size() || stored != expected) {
        // Only the matching pending move can repair an interrupted prefix.
        if (retired && context.recoverRetired && source == context.expectedSource &&
            completionHash == context.originHash && sealRead == FileResult::Read && sealSize < expected.size() &&
            std::memcmp(stored.data(), expected.data(), sealSize) == 0)
          continue;
        context.result = FindResult::Blocked;
        return true;
      }
    }
    if (source != context.expectedSource || (context.recoverRetired && completionHash != context.originHash) ||
        (!context.recoverRetired && (retirements & bit)))
      continue;
    if (!(seals & bit)) {
      context.result = FindResult::Blocked;
      return true;
    }
    if (matching != 64) {
      context.result = FindResult::Conflict;
      return true;
    }
    matching = slot;
  }
  if (matching == 64) {
    context.result = FindResult::Missing;
    return true;
  }
  char path[64];
  pathFor(matching, ".defer-", path);
  size_t actualSize = 0;
  if (readFile(path, actual->data(), actual->size(), actualSize) != FileResult::Read) return true;
  uint64_t completionHash = 0;
  std::string_view source;
  if (!decodeWithHash(actual->data(), actualSize, completionHash, source) || source != context.expectedSource)
    return true;
  context.output = *actual;
  context.size = actualSize;
  context.slot = matching;
  context.result = FindResult::Found;
  return true;
}

bool findReadyLocked(void* raw) {
  auto& context = *static_cast<FindContext*>(raw);
  auto record = makeUniqueNoThrow<Record>();
  if (!record) return true;
  size_t size = 0;
  unsigned slot = 0;
  FindContext found{context.expectedSource, *record, size, slot};
  findLocked(&found);
  if (found.result != FindResult::Found) {
    context.result = found.result;
    return true;
  }
  uint64_t completionHash = 0;
  std::string_view source;
  if (!decodeWithHash(record->data(), size, completionHash, source)) return true;
  const auto proof = cj::deferredRetirementProof(source, completionHash);
  switch (proof) {
    case cj::DeferredRetirementProof::Retired:
      break;
    case cj::DeferredRetirementProof::Busy:
      context.result = FindResult::Busy;
      return true;
    case cj::DeferredRetirementProof::Missing:
    case cj::DeferredRetirementProof::Conflict:
    case cj::DeferredRetirementProof::Damaged:
      context.result = FindResult::Blocked;
      return true;
    case cj::DeferredRetirementProof::Unavailable:
      return true;
  }
  context.output = *record;
  context.size = size;
  context.slot = slot;
  context.result = FindResult::Found;
  return true;
}

struct DiscoverContext {
  Record& output;
  size_t& size;
  unsigned& slot;
  FindResult result = FindResult::Unavailable;
};

bool discoverReadyLocked(void* raw) {
  auto& context = *static_cast<DiscoverContext*>(raw);
  if (StatsSessionLease::anyActive()) {
    context.result = FindResult::Busy;
    return true;
  }
  const auto directoryStatus = Storage.lookupPath(DIRECTORY);
  if (directoryStatus == HalStorage::PathStatus::Missing) {
    context.result = FindResult::Missing;
    return true;
  }
  if (directoryStatus != HalStorage::PathStatus::Present) return true;
  uint64_t records = 0, seals = 0, retirements = 0;
  if (!Storage.lookupNumberedChildren(DIRECTORY, ".defer-", records) ||
      !Storage.lookupNumberedChildren(DIRECTORY, ".dseal-", seals) ||
      !Storage.lookupNumberedChildren(DIRECTORY, ".dretire-", retirements))
    return true;
  if ((seals & ~records) || (retirements & ~(records & seals)) || (records & ~seals)) {
    context.result = FindResult::Blocked;
    return true;
  }
  if (records) {
    FindContext audit{"", context.output, context.size, context.slot};
    findLocked(&audit);
    if (audit.result != FindResult::Missing) {
      context.result = audit.result;
      return true;
    }
  }
  const uint64_t active = records & seals & ~retirements;
  if (!active) {
    context.result = FindResult::Missing;
    return true;
  }
  unsigned first = 0;
  while (!(active & (uint64_t{1} << first))) ++first;
  auto candidate = makeUniqueNoThrow<Record>();
  if (!candidate) return true;
  char path[64];
  pathFor(first, ".defer-", path);
  size_t candidateSize = 0;
  const auto read = readFile(path, candidate->data(), candidate->size(), candidateSize);
  if (read == FileResult::Unavailable || read == FileResult::Missing) return true;
  uint64_t hash = 0;
  std::string_view source;
  if (read != FileResult::Read || !decodeWithHash(candidate->data(), candidateSize, hash, source)) {
    context.result = FindResult::Blocked;
    return true;
  }
  FindContext found{source, context.output, context.size, context.slot};
  findReadyLocked(&found);
  context.result = found.result;
  return true;
}

bool findForMoveRecoveryLocked(void* raw) {
  auto& context = *static_cast<FindContext*>(raw);
  uint64_t originHash = 0;
  {
    auto journal = makeUniqueNoThrow<cj::Record>();
    if (!journal) return true;
    size_t size = 0;
    unsigned slot = 0;
    const auto scan = cj::scanRecords(*journal, size, slot);
    if (scan == cj::ScanResult::Empty || scan == cj::ScanResult::PreparedOnly) {
      context.result = FindResult::Missing;
      return true;
    }
    if (scan == cj::ScanResult::Conflict || scan == cj::ScanResult::Damaged) {
      context.result = FindResult::Blocked;
      return true;
    }
    if (scan != cj::ScanResult::Candidate) return true;
    std::string_view source;
    if (!cj::decodeDeferredMove(journal->data(), size, source, originHash) || source != context.expectedSource) {
      context.result = FindResult::Conflict;
      return true;
    }
  }
  context.recoverRetired = true;
  context.originHash = originHash;
  return findLocked(&context);
}

bool discoverResumableLocked(void* raw) {
  auto& context = *static_cast<DiscoverContext*>(raw);
  if (StatsSessionLease::anyActive()) {
    context.result = FindResult::Busy;
    return true;
  }
  const auto status = Storage.lookupPath("/.completion-journal");
  if (status == HalStorage::PathStatus::Missing) return discoverReadyLocked(raw);
  if (status != HalStorage::PathStatus::Present) return true;
  auto journal = makeUniqueNoThrow<cj::Record>();
  if (!journal) return true;
  size_t size = 0;
  unsigned slot = 0;
  switch (cj::scanRecords(*journal, size, slot)) {
    case cj::ScanResult::Empty:
    case cj::ScanResult::PreparedOnly:
      journal.reset();
      return discoverReadyLocked(raw);
    case cj::ScanResult::Conflict:
    case cj::ScanResult::Damaged:
      context.result = FindResult::Blocked;
      return true;
    case cj::ScanResult::Unavailable:
      return true;
    case cj::ScanResult::Candidate:
      break;
  }
  std::string_view source;
  uint64_t origin = 0;
  if (!cj::decodeDeferredMove(journal->data(), size, source, origin)) {
    context.result = FindResult::Busy;
    return true;
  }
  FindContext found{source, context.output, context.size, context.slot};
  findForMoveRecoveryLocked(&found);
  context.result = found.result == FindResult::Missing ? FindResult::Blocked : found.result;
  return true;
}

bool publishLocked(void* raw) {
  auto& context = *static_cast<Context*>(raw);
  if (StatsSessionLease::anyActive()) return true;
  const auto journalStatus = Storage.lookupPath("/.completion-journal");
  if (journalStatus == HalStorage::PathStatus::Missing) {
    context.result = PublishResult::NoPending;
    return true;
  }
  if (journalStatus != HalStorage::PathStatus::Present) return true;
  auto journal = makeUniqueNoThrow<cj::Record>();
  if (!journal) return true;
  size_t journalSize = 0;
  unsigned journalSlot = 0;
  const auto state = cj::scanRecords(*journal, journalSize, journalSlot);
  if (state == cj::ScanResult::Empty || state == cj::ScanResult::PreparedOnly) {
    context.result = PublishResult::NoPending;
    return true;
  }
  if (state == cj::ScanResult::Conflict || state == cj::ScanResult::Damaged) {
    context.result = PublishResult::Blocked;
    return true;
  }
  if (state != cj::ScanResult::Candidate) return true;
  std::string_view source, directory;
  const uint8_t *book = nullptr, *global = nullptr;
  if (!cj::decode(journal->data(), journalSize, directory, book, global) ||
      !cj::decodeDeferredSource(journal->data(), journalSize, source) || !book[11] ||
      source != context.expectedSource) {
    context.result = PublishResult::Conflict;
    return true;
  }
  cj::SnapshotState bookState = cj::SnapshotState::Before, globalState = cj::SnapshotState::Before;
  const auto replay = cj::checkReplay(bookState, globalState);
  if (replay == cj::ReplayCheck::Unavailable) return true;
  if (replay != cj::ReplayCheck::Ready || bookState != cj::SnapshotState::After ||
      globalState != cj::SnapshotState::After) {
    context.result = PublishResult::Blocked;
    return true;
  }
  const uint64_t completionHash = cj::checksum(journal->data(), journalSize);
  auto wanted = makeUniqueNoThrow<Record>();
  auto found = makeUniqueNoThrow<Record>();
  if (!wanted || !found) return true;
  size_t wantedSize = 0;
  if (!encode(source, completionHash, *wanted, wantedSize)) return true;
  const auto directoryStatus = Storage.lookupPath(DIRECTORY);
  if (directoryStatus == HalStorage::PathStatus::Failed) return true;
  if (directoryStatus == HalStorage::PathStatus::Missing &&
      (!Storage.mkdir(DIRECTORY, false) || Storage.lookupPath(DIRECTORY) != HalStorage::PathStatus::Present))
    return true;
  uint64_t records = 0, seals = 0, retirements = 0;
  if (!Storage.lookupNumberedChildren(DIRECTORY, ".defer-", records) ||
      !Storage.lookupNumberedChildren(DIRECTORY, ".dseal-", seals) ||
      !Storage.lookupNumberedChildren(DIRECTORY, ".dretire-", retirements))
    return true;
  if ((seals & ~records) || (retirements & ~(records & seals))) {
    context.result = PublishResult::Blocked;
    return true;
  }
  unsigned matching = 64;
  for (unsigned slot = 0; slot < 64; ++slot) {
    const uint64_t bit = uint64_t{1} << slot;
    if (!(records & bit)) continue;
    char path[64];
    pathFor(slot, ".defer-", path);
    size_t actualSize = 0;
    const auto read = readFile(path, found->data(), found->size(), actualSize);
    if (read == FileResult::Unavailable || read == FileResult::Missing) return true;
    uint64_t foundHash = 0;
    std::string_view foundSource;
    if (read != FileResult::Read || !decodeWithHash(found->data(), actualSize, foundHash, foundSource)) {
      context.result = PublishResult::Blocked;
      return true;
    }
    for (const bool retired : {false, true}) {
      if (!((retired ? retirements : seals) & bit)) continue;
      Seal expected{}, actual{};
      if (!encodeSeal(slot, *found, actualSize, expected, retired)) return true;
      pathFor(slot, retired ? ".dretire-" : ".dseal-", path);
      size_t sealSize = 0;
      const auto sealRead = readFile(path, actual.data(), actual.size(), sealSize);
      if (sealRead == FileResult::Unavailable || sealRead == FileResult::Missing) return true;
      if (sealRead != FileResult::Read || sealSize != actual.size() || actual != expected) {
        context.result = PublishResult::Blocked;
        return true;
      }
    }
    if (retirements & bit) continue;
    const bool same = actualSize == wantedSize && std::memcmp(found->data(), wanted->data(), wantedSize) == 0;
    if (foundSource == source && !same) {
      context.result = PublishResult::Conflict;
      return true;
    }
    if (same) {
      if (matching != 64) {
        context.result = PublishResult::Conflict;
        return true;
      }
      matching = slot;
    }
  }
  if (matching < 64 && (seals & (uint64_t{1} << matching))) {
    context.result = PublishResult::AlreadyPublished;
    return true;
  }
  if (matching == 64) {
    unsigned freeSlot = 0;
    while (freeSlot < 64 && (records & (uint64_t{1} << freeSlot))) ++freeSlot;
    if (freeSlot == 64) {
      context.result = PublishResult::Full;
      return true;
    }
    matching = freeSlot;
    char path[64];
    pathFor(matching, ".defer-", path);
    if (!createFile(path, wanted->data(), wantedSize)) return true;
  }
  Seal seal{};
  if (!encodeSeal(matching, *wanted, wantedSize, seal)) return true;
  char path[64];
  pathFor(matching, ".dseal-", path);
  if (!createFile(path, seal.data(), seal.size())) return true;
  context.result = PublishResult::Published;
  return true;
}
}  // namespace

PublishResult publish(std::string_view expectedSource) {
  Context context{expectedSource};
  return Storage.withStorageLock(publishLocked, &context) ? context.result : PublishResult::Unavailable;
}

FindResult find(std::string_view expectedSource, Record& output, size_t& size, unsigned& slot) {
  FindContext context{expectedSource, output, size, slot};
  return Storage.withStorageLock(findLocked, &context) ? context.result : FindResult::Unavailable;
}

FindResult findForMoveRecovery(std::string_view expectedSource, Record& output, size_t& size, unsigned& slot) {
  FindContext context{expectedSource, output, size, slot};
  return Storage.withStorageLock(findForMoveRecoveryLocked, &context) ? context.result : FindResult::Unavailable;
}

FindResult findReady(std::string_view expectedSource, Record& output, size_t& size, unsigned& slot) {
  FindContext context{expectedSource, output, size, slot};
  return Storage.withStorageLock(findReadyLocked, &context) ? context.result : FindResult::Unavailable;
}

FindResult discoverReady(Record& output, size_t& size, unsigned& slot) {
  DiscoverContext context{output, size, slot};
  return Storage.withStorageLock(discoverReadyLocked, &context) ? context.result : FindResult::Unavailable;
}

FindResult discoverResumable(Record& output, size_t& size, unsigned& slot) {
  DiscoverContext context{output, size, slot};
  return Storage.withStorageLock(discoverResumableLocked, &context) ? context.result : FindResult::Unavailable;
}
}  // namespace readingstats::deferredcompletion
