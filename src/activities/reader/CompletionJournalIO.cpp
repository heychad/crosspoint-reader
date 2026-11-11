#include "CompletionJournalIO.h"

#include <HalStorage.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CompletionArchiveInventory.h"
#include "CompletionSnapshotValidation.h"
#include "StatsSafeReplace.h"

namespace readingstats::completionjournal {
namespace {
struct CreateContext {
  unsigned slot;
  const char* prefix;
  const uint8_t* bytes;
  size_t size;
  CreateResult result = CreateResult::Uncertain;
  bool durable = false;
};

bool createLocked(void* raw) {
  auto& context = *static_cast<CreateContext*>(raw);
  char path[64];
  snprintf(path, sizeof(path), "/.completion-journal/%s%02u", context.prefix, context.slot);
  const auto status = Storage.lookupPath(path);
  if (status == HalStorage::PathStatus::Present) {
    context.result = CreateResult::Occupied;
    return true;
  }
  if (status != HalStorage::PathStatus::Missing) return true;
  HalFile file = Storage.openChecked(path, O_WRONLY | O_CREAT | O_EXCL);
  if (!file) return true;
  const bool written = file.write(context.bytes, context.size) == context.size;
  const bool synced = file.sync();
  const bool closed = file.close();
  if (!written || !synced || !closed) return true;
  context.durable = true;

  HalFile check;
  if (!Storage.openFileForRead("STATS", path, check)) return true;
  bool same = check.fileSize() == context.size;
  uint8_t buffer[64];
  for (size_t offset = 0; same && offset < context.size;) {
    const size_t count = std::min(sizeof(buffer), context.size - offset);
    same =
        check.read(buffer, count) == static_cast<int>(count) && std::memcmp(buffer, context.bytes + offset, count) == 0;
    offset += count;
  }
  const bool checkedClose = check.close();
  if (same && checkedClose) context.result = CreateResult::Created;
  return true;
}

struct InspectContext {
  unsigned slot;
  Record& output;
  size_t& size;
  InspectResult result = InspectResult::Unavailable;
  const char* directory = "/.completion-journal";
};

bool inspectLocked(void* raw) {
  auto& context = *static_cast<InspectContext*>(raw);
  char path[64];
  snprintf(path, sizeof(path), "%s/.completion-%02u", context.directory, context.slot);
  const auto status = Storage.lookupPath(path);
  if (status == HalStorage::PathStatus::Missing) {
    context.result = InspectResult::Missing;
    return true;
  }
  if (status != HalStorage::PathStatus::Present) return true;
  HalFile file;
  if (!Storage.openFileForRead("STATS", path, file)) return true;
  const auto length = file.fileSize();
  if (length > MAX_RECORD) {
    if (file.close()) context.result = InspectResult::Damaged;
    return true;
  }
  auto candidate = makeUniqueNoThrow<Record>();
  if (!candidate) {
    file.close();
    return true;
  }
  const int read = file.read(candidate->data(), length);
  const bool closed = file.close();
  if (read != static_cast<int>(length) || !closed) return true;
  std::string_view directory;
  const uint8_t *book = nullptr, *global = nullptr;
  if (!decode(candidate->data(), length, directory, book, global)) {
    context.result = InspectResult::Damaged;
    return true;
  }
  context.output = *candidate;
  context.size = length;
  context.result = InspectResult::Valid;
  return true;
}

struct SealContext {
  unsigned slot;
  const Record& record;
  size_t size;
  CreateResult result = CreateResult::Uncertain;
};

bool sealLocked(void* raw) {
  auto& context = *static_cast<SealContext*>(raw);
  auto actual = makeUniqueNoThrow<Record>();
  if (!actual) return true;
  size_t actualSize = 0;
  InspectContext inspect{context.slot, *actual, actualSize};
  inspectLocked(&inspect);
  if (inspect.result != InspectResult::Valid || actualSize != context.size ||
      std::memcmp(actual->data(), context.record.data(), actualSize) != 0)
    return true;
  Seal seal{};
  if (!encodeSeal(context.slot, *actual, actualSize, seal)) return true;
  CreateContext create{context.slot, ".cseal-", seal.data(), seal.size()};
  createLocked(&create);
  context.result = create.result;
  return true;
}

struct InspectSealContext {
  unsigned slot;
  const Record& record;
  size_t size;
  bool retirement = false;
  SealResult result = SealResult::Unavailable;
  const char* directory = "/.completion-journal";
};

bool inspectSealLocked(void* raw) {
  auto& context = *static_cast<InspectSealContext*>(raw);
  char path[64];
  snprintf(path, sizeof(path), "%s/%s%02u", context.directory, context.retirement ? ".cretire-" : ".cseal-",
           context.slot);
  const auto status = Storage.lookupPath(path);
  if (status == HalStorage::PathStatus::Missing) {
    context.result = SealResult::Missing;
    return true;
  }
  if (status != HalStorage::PathStatus::Present) return true;
  HalFile file;
  if (!Storage.openFileForRead("STATS", path, file)) return true;
  Seal actual{};
  if (file.fileSize() != actual.size()) {
    if (file.close()) context.result = SealResult::Damaged;
    return true;
  }
  const int read = file.read(actual.data(), actual.size());
  const bool closed = file.close();
  if (read != static_cast<int>(actual.size()) || !closed) return true;
  const bool matches = context.retirement ? matchesRetirement(context.slot, context.record, context.size, actual)
                                          : matchesSeal(context.slot, context.record, context.size, actual);
  context.result = matches ? SealResult::Matching : SealResult::Damaged;
  return true;
}

struct ScanContext {
  Record& output;
  size_t& size;
  unsigned& slot;
  ScanResult result = ScanResult::Unavailable;
};

bool scanLocked(void* raw) {
  auto& context = *static_cast<ScanContext*>(raw);
  uint64_t present = 0;
  if (!Storage.lookupNumberedChildren("/.completion-journal", ".completion-", present)) return true;
  uint64_t seals = 0;
  if (!Storage.lookupNumberedChildren("/.completion-journal", ".cseal-", seals)) return true;
  uint64_t retired = 0;
  if (!Storage.lookupNumberedChildren("/.completion-journal", ".cretire-", retired)) return true;
  if ((seals & ~present) != 0 || (retired & ~(present & seals)) != 0) {
    context.result = ScanResult::Damaged;
    return true;
  }
  if (!present && !seals && !retired) {
    context.result = ScanResult::Empty;
    return true;
  }
  auto current = makeUniqueNoThrow<Record>();
  if (!current) return true;
  unsigned candidateSlot = 0;
  bool found = false;
  bool prepared = false;
  for (unsigned slot = 0; slot < 64; ++slot) {
    if ((present & (uint64_t{1} << slot)) == 0) continue;
    size_t currentSize = 0;
    InspectContext inspect{slot, *current, currentSize};
    inspectLocked(&inspect);
    switch (inspect.result) {
      case InspectResult::Missing:
        return true;
      case InspectResult::Valid:
        if ((seals & (uint64_t{1} << slot)) == 0) {
          prepared = true;
          break;
        }
        {
          InspectSealContext seal{slot, *current, currentSize};
          inspectSealLocked(&seal);
          if (seal.result == SealResult::Damaged) {
            context.result = ScanResult::Damaged;
            return true;
          }
          if (seal.result != SealResult::Matching) return true;
          std::string_view directory;
          const uint8_t *book = nullptr, *global = nullptr;
          if (!decode(current->data(), currentSize, directory, book, global) || !validSnapshotPayload(book, global)) {
            context.result = ScanResult::Damaged;
            return true;
          }
          if (retired & (uint64_t{1} << slot)) {
            InspectSealContext receipt{slot, *current, currentSize, true};
            inspectSealLocked(&receipt);
            if (receipt.result == SealResult::Damaged) {
              context.result = ScanResult::Damaged;
              return true;
            }
            if (receipt.result != SealResult::Matching) return true;
            break;
          }
        }
        if (found) {
          context.result = ScanResult::Conflict;
          return true;
        }
        candidateSlot = slot;
        found = true;
        break;
      case InspectResult::Damaged:
        context.result = ScanResult::Damaged;
        return true;
      case InspectResult::Unavailable:
      case InspectResult::InvalidSlot:
        return true;
    }
  }
  if (found) {
    size_t candidateSize = 0;
    InspectContext inspect{candidateSlot, *current, candidateSize};
    inspectLocked(&inspect);
    if (inspect.result != InspectResult::Valid) return true;
    InspectSealContext seal{candidateSlot, *current, candidateSize};
    inspectSealLocked(&seal);
    if (seal.result != SealResult::Matching) return true;
    std::string_view directory;
    const uint8_t *book = nullptr, *global = nullptr;
    if (!decode(current->data(), candidateSize, directory, book, global) || !validSnapshotPayload(book, global)) {
      context.result = ScanResult::Damaged;
      return true;
    }
    context.output = *current;
    context.size = candidateSize;
    context.slot = candidateSlot;
    context.result = ScanResult::Candidate;
  } else {
    context.result = prepared ? ScanResult::PreparedOnly : ScanResult::Empty;
  }
  return true;
}

enum class Comparison { Before, After, Conflict, Unavailable };

Comparison compareFile(const char* path, const uint8_t* before, size_t beforeSize, const uint8_t* after,
                       size_t afterSize) {
  char temporary[MAX_PATH + 20];
  const int tempLength = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
  if (tempLength < 0 || static_cast<size_t>(tempLength) >= sizeof(temporary)) return Comparison::Unavailable;
  const auto tempStatus = Storage.lookupPath(temporary);
  if (tempStatus == HalStorage::PathStatus::Failed) return Comparison::Unavailable;
  if (tempStatus == HalStorage::PathStatus::Present) return Comparison::Conflict;
  const auto status = Storage.lookupPath(path);
  if (status == HalStorage::PathStatus::Failed) return Comparison::Unavailable;
  if (status == HalStorage::PathStatus::Missing) return beforeSize == 0 ? Comparison::Before : Comparison::Conflict;
  HalFile file;
  if (!Storage.openFileForRead("STATS", path, file)) return Comparison::Unavailable;
  const size_t length = file.fileSize();
  bool matchesBefore = beforeSize != 0 && length == beforeSize;
  bool matchesAfter = length == afterSize;
  bool ok = true;
  uint8_t buffer[64];
  for (size_t offset = 0; ok && (matchesBefore || matchesAfter) && offset < length;) {
    const size_t count = std::min(sizeof(buffer), length - offset);
    if (file.read(buffer, count) != static_cast<int>(count)) {
      ok = false;
      break;
    }
    if (matchesBefore && std::memcmp(buffer, before + offset, count) != 0) matchesBefore = false;
    if (matchesAfter && std::memcmp(buffer, after + offset, count) != 0) matchesAfter = false;
    offset += count;
  }
  const bool closed = file.close();
  if (!ok || !closed) return Comparison::Unavailable;
  if (matchesAfter) return Comparison::After;
  if (matchesBefore) return Comparison::Before;
  return Comparison::Conflict;
}

struct ReplayCheckContext {
  SnapshotState& bookState;
  SnapshotState& globalState;
  Record* record = nullptr;
  size_t* size = nullptr;
  unsigned* slot = nullptr;
  ReplayCheck result = ReplayCheck::Unavailable;
};

bool checkReplayLocked(void* raw) {
  auto& context = *static_cast<ReplayCheckContext*>(raw);
  auto record = makeUniqueNoThrow<Record>();
  if (!record) return true;
  size_t size = 0;
  unsigned slot = 0;
  ScanContext scan{*record, size, slot};
  scanLocked(&scan);
  if (scan.result == ScanResult::Empty || scan.result == ScanResult::PreparedOnly) {
    context.result = ReplayCheck::NoCandidate;
    return true;
  }
  if (scan.result == ScanResult::Conflict || scan.result == ScanResult::Damaged) {
    context.result = ReplayCheck::Blocked;
    return true;
  }
  if (scan.result != ScanResult::Candidate) return true;
  std::string_view directory;
  const uint8_t *book = nullptr, *global = nullptr, *oldBook = nullptr, *oldGlobal = nullptr;
  size_t oldBookSize = 0, oldGlobalSize = 0;
  if (!decode(record->data(), size, directory, book, global) ||
      !decodeBaseline(record->data(), size, oldBook, oldBookSize, oldGlobal, oldGlobalSize)) {
    context.result = ReplayCheck::Blocked;
    return true;
  }
  char bookPath[MAX_PATH + 16];
  const int pathLength =
      snprintf(bookPath, sizeof(bookPath), "%.*s/stats_v5.bin", static_cast<int>(directory.size()), directory.data());
  if (pathLength < 0 || static_cast<size_t>(pathLength) >= sizeof(bookPath)) return true;
  const auto bookMatch = compareFile(bookPath, oldBook, oldBookSize, book, BOOK_BYTES);
  const auto globalMatch = compareFile("/.crosspoint/global_stats.bin", oldGlobal, oldGlobalSize, global, GLOBAL_BYTES);
  if (bookMatch == Comparison::Unavailable || globalMatch == Comparison::Unavailable) return true;
  if (bookMatch == Comparison::Conflict || globalMatch == Comparison::Conflict) {
    context.result = ReplayCheck::Conflict;
    return true;
  }
  context.bookState = bookMatch == Comparison::After ? SnapshotState::After : SnapshotState::Before;
  context.globalState = globalMatch == Comparison::After ? SnapshotState::After : SnapshotState::Before;
  if (context.record && context.size) {
    *context.record = *record;
    *context.size = size;
  }
  if (context.slot) *context.slot = slot;
  context.result = ReplayCheck::Ready;
  return true;
}

struct ReplayContext {
  ReplayResult result = ReplayResult::Unavailable;
};

bool replayLocked(void* raw) {
  auto& context = *static_cast<ReplayContext*>(raw);
  auto record = makeUniqueNoThrow<Record>();
  if (!record) return true;
  size_t size = 0;
  SnapshotState bookState = SnapshotState::Before, globalState = SnapshotState::Before;
  ReplayCheckContext check{bookState, globalState, record.get(), &size};
  checkReplayLocked(&check);
  switch (check.result) {
    case ReplayCheck::NoCandidate:
      context.result = ReplayResult::NoCandidate;
      return true;
    case ReplayCheck::Conflict:
      context.result = ReplayResult::Conflict;
      return true;
    case ReplayCheck::Blocked:
      context.result = ReplayResult::Blocked;
      return true;
    case ReplayCheck::Unavailable:
      return true;
    case ReplayCheck::Ready:
      break;
  }
  std::string_view directory;
  const uint8_t *book = nullptr, *global = nullptr, *oldBook = nullptr, *oldGlobal = nullptr;
  size_t oldBookSize = 0, oldGlobalSize = 0;
  if (!decode(record->data(), size, directory, book, global) ||
      !decodeBaseline(record->data(), size, oldBook, oldBookSize, oldGlobal, oldGlobalSize)) {
    context.result = ReplayResult::Blocked;
    return true;
  }
  // Missing canonical files may have an authoritative legacy source. This
  // prototype cannot yet establish that source's identity before replay.
  if (oldBookSize != BOOK_BYTES || oldGlobalSize != GLOBAL_BYTES || !validSnapshotPayload(oldBook, oldGlobal)) {
    context.result = ReplayResult::Blocked;
    return true;
  }
  const std::string bookPath = std::string(directory) + "/stats_v5.bin";
  if ((bookState == SnapshotState::Before && !statsreplace::save("STATS", bookPath, book, BOOK_BYTES, true)) ||
      (globalState == SnapshotState::Before &&
       !statsreplace::save("GSTATS", "/.crosspoint/global_stats.bin", global, GLOBAL_BYTES, true))) {
    context.result = ReplayResult::Incomplete;
    return true;
  }
  SnapshotState verifiedBook = SnapshotState::Before, verifiedGlobal = SnapshotState::Before;
  ReplayCheckContext verify{verifiedBook, verifiedGlobal};
  checkReplayLocked(&verify);
  if (verify.result != ReplayCheck::Ready || verifiedBook != SnapshotState::After ||
      verifiedGlobal != SnapshotState::After) {
    context.result = ReplayResult::Incomplete;
    return true;
  }
  context.result = ReplayResult::AppliedUnretired;
  return true;
}

struct RetirementContext {
  RetirementResult result = RetirementResult::Unavailable;
};

bool retireLocked(void* raw) {
  auto& context = *static_cast<RetirementContext*>(raw);
  auto record = makeUniqueNoThrow<Record>();
  if (!record) return true;
  size_t size = 0;
  unsigned slot = 0;
  SnapshotState bookState = SnapshotState::Before, globalState = SnapshotState::Before;
  ReplayCheckContext check{bookState, globalState, record.get(), &size, &slot};
  checkReplayLocked(&check);
  switch (check.result) {
    case ReplayCheck::NoCandidate:
      context.result = RetirementResult::NoCandidate;
      return true;
    case ReplayCheck::Conflict:
      context.result = RetirementResult::Conflict;
      return true;
    case ReplayCheck::Blocked:
      context.result = RetirementResult::Blocked;
      return true;
    case ReplayCheck::Unavailable:
      return true;
    case ReplayCheck::Ready:
      break;
  }
  if (bookState != SnapshotState::After || globalState != SnapshotState::After) {
    context.result = RetirementResult::NotApplied;
    return true;
  }
  Seal receipt{};
  if (!encodeRetirement(slot, *record, size, receipt)) return true;
  CreateContext create{slot, ".cretire-", receipt.data(), receipt.size()};
  createLocked(&create);
  if (create.result == CreateResult::Created) {
    context.result = RetirementResult::Retired;
  } else if (create.durable) {
    // A transient verification read can fail after a successful sync and close.
    // Only acknowledge the exact receipt if a fresh independent read matches.
    InspectSealContext verify{slot, *record, size, true};
    inspectSealLocked(&verify);
    context.result = verify.result == SealResult::Matching ? RetirementResult::Retired : RetirementResult::Uncertain;
  } else {
    context.result = RetirementResult::Uncertain;
  }
  return true;
}

struct DeferredProofContext {
  std::string_view source;
  uint64_t completionHash;
  uint8_t* policy;
  DeferredRetirementProof result = DeferredRetirementProof::Unavailable;
  bool deferredMove = false;
  uint64_t originHash = 0;
  RetiredJournalLocation* location = nullptr;
};

DeferredRetirementProof proofInDirectory(const char* directory, DeferredProofContext& context, bool archived) {
  if (archived) {
    char markerPath[64];
    snprintf(markerPath, sizeof(markerPath), "%s/.archive-00", directory);
    const auto markerStatus = Storage.lookupPath(markerPath);
    if (markerStatus == HalStorage::PathStatus::Missing) return DeferredRetirementProof::Damaged;
    if (markerStatus != HalStorage::PathStatus::Present) return DeferredRetirementProof::Unavailable;
    HalFile marker;
    if (!Storage.openFileForRead("STATS", markerPath, marker)) return DeferredRetirementProof::Unavailable;
    constexpr uint8_t expected[] = {'C', 'P', 'J', 'A', 1, 0, 0, 0};
    uint8_t actual[sizeof(expected)]{};
    const bool correctSize = marker.fileSize() == sizeof(actual);
    const bool read = correctSize && marker.read(actual, sizeof(actual)) == static_cast<int>(sizeof(actual));
    if (!marker.close() || (correctSize && !read)) return DeferredRetirementProof::Unavailable;
    if (!correctSize || std::memcmp(actual, expected, sizeof(actual)) != 0) return DeferredRetirementProof::Damaged;
  }
  uint64_t receipts = 0;
  if (!Storage.lookupNumberedChildren(directory, ".cretire-", receipts)) return DeferredRetirementProof::Unavailable;
  if (archived) {
    uint64_t records = 0, seals = 0;
    if (!Storage.lookupNumberedChildren(directory, ".completion-", records) ||
        !Storage.lookupNumberedChildren(directory, ".cseal-", seals))
      return DeferredRetirementProof::Unavailable;
    if (records != UINT64_MAX || seals != records || receipts != records) return DeferredRetirementProof::Damaged;
  }
  auto record = makeUniqueNoThrow<Record>();
  if (!record) return DeferredRetirementProof::Unavailable;
  bool found = false;
  RetiredJournalLocation matching{};
  for (unsigned slot = 0; slot < 64; ++slot) {
    if (!(receipts & (uint64_t{1} << slot))) continue;
    size_t recordSize = 0;
    InspectContext inspect{slot, *record, recordSize};
    inspect.directory = directory;
    inspectLocked(&inspect);
    if (inspect.result == InspectResult::Unavailable || inspect.result == InspectResult::Missing)
      return DeferredRetirementProof::Unavailable;
    if (inspect.result != InspectResult::Valid) return DeferredRetirementProof::Damaged;
    if (checksum(record->data(), recordSize) != context.completionHash) continue;
    std::string_view source;
    if (context.deferredMove) {
      uint64_t originHash = 0;
      if (!decodeDeferredMove(record->data(), recordSize, source, originHash) || source != context.source ||
          originHash != context.originHash)
        continue;
    } else if (!decodeDeferredSource(record->data(), recordSize, source) || source != context.source) {
      continue;
    }
    InspectSealContext seal{slot, *record, recordSize};
    InspectSealContext retirement{slot, *record, recordSize, true};
    seal.directory = directory;
    retirement.directory = directory;
    inspectSealLocked(&seal);
    inspectSealLocked(&retirement);
    if (seal.result == SealResult::Unavailable || retirement.result == SealResult::Unavailable)
      return DeferredRetirementProof::Unavailable;
    if (seal.result != SealResult::Matching || retirement.result != SealResult::Matching)
      return DeferredRetirementProof::Damaged;
    if (context.deferredMove && found) return DeferredRetirementProof::Conflict;
    found = true;
    if (context.deferredMove || context.location) {
      const auto length = std::strlen(directory);
      if (length >= matching.directory.size()) return DeferredRetirementProof::Unavailable;
      std::memcpy(matching.directory.data(), directory, length + 1);
      matching.slot = slot;
    }
    if (context.policy) *context.policy = (*record)[9];
    if (!context.deferredMove) {
      if (context.location) *context.location = matching;
      return DeferredRetirementProof::Retired;
    }
  }
  if (!found) return DeferredRetirementProof::Missing;
  if (context.location) *context.location = matching;
  return DeferredRetirementProof::Retired;
}

bool deferredProofLocked(void* raw) {
  auto& context = *static_cast<DeferredProofContext*>(raw);
  bool found = false;
  RetiredJournalLocation matching{};
  const auto inspect = [&](const char* directory, bool archived) {
    RetiredJournalLocation candidate{};
    DeferredProofContext probe = context;
    probe.location = &candidate;
    const auto proof = proofInDirectory(directory, probe, archived);
    if (proof != DeferredRetirementProof::Retired) return proof;
    if (!context.deferredMove) {
      if (context.location) *context.location = candidate;
      return proof;
    }
    if (found) return DeferredRetirementProof::Conflict;
    found = true;
    matching = candidate;
    return DeferredRetirementProof::Missing;
  };
  const auto presence = Storage.lookupPath("/.completion-journal");
  if (presence == HalStorage::PathStatus::Failed) return true;
  if (presence == HalStorage::PathStatus::Present) {
    {
      auto record = makeUniqueNoThrow<Record>();
      if (!record) return true;
      size_t size = 0;
      unsigned candidateSlot = 0;
      ScanContext scan{*record, size, candidateSlot};
      scanLocked(&scan);
      if (scan.result == ScanResult::Candidate) {
        context.result = DeferredRetirementProof::Busy;
        return true;
      }
      if (scan.result == ScanResult::Conflict || scan.result == ScanResult::Damaged) {
        context.result = DeferredRetirementProof::Damaged;
        return true;
      }
      if (scan.result == ScanResult::Unavailable) return true;
    }
    const auto proof = inspect("/.completion-journal", false);
    if (proof != DeferredRetirementProof::Missing) {
      context.result = proof;
      return true;
    }
  }
  ArchiveInventory archives{};
  switch (inventoryArchives(Storage, archives)) {
    case ArchiveInventoryResult::Conflict:
      context.result = DeferredRetirementProof::Damaged;
      return true;
    case ArchiveInventoryResult::Unavailable:
      return true;
    case ArchiveInventoryResult::Ready:
      break;
  }
  for (unsigned index = 0; index < 1024; ++index) {
    if (!hasArchive(archives, index)) continue;
    char archive[48];
    snprintf(archive, sizeof(archive), "/.completion-history-%06u", index);
    const auto status = Storage.lookupPath(archive);
    if (status != HalStorage::PathStatus::Present) return true;
    const auto proof = inspect(archive, true);
    if (proof != DeferredRetirementProof::Missing) {
      context.result = proof;
      return true;
    }
  }
  context.result = found ? DeferredRetirementProof::Retired : DeferredRetirementProof::Missing;
  if (found && context.location) *context.location = matching;
  return true;
}

struct StartContext {
  const Record& record;
  size_t size;
  StartResult result = StartResult::Unavailable;
};

// The marker closes a full, fully retired journal before it is renamed. A
// retry must finish this rollover before it can append another transaction.
constexpr uint8_t ARCHIVE_MARKER[] = {'C', 'P', 'J', 'A', 1, 0, 0, 0};
constexpr char ARCHIVE_MARKER_PATH[] = "/.completion-journal/.archive-00";

enum class ArchiveMarker { Missing, Valid, Damaged, Unavailable };
ArchiveMarker inspectArchiveMarker() {
  const auto status = Storage.lookupPath(ARCHIVE_MARKER_PATH);
  if (status == HalStorage::PathStatus::Missing) return ArchiveMarker::Missing;
  if (status != HalStorage::PathStatus::Present) return ArchiveMarker::Unavailable;
  HalFile file;
  if (!Storage.openFileForRead("STATS", ARCHIVE_MARKER_PATH, file)) return ArchiveMarker::Unavailable;
  uint8_t bytes[sizeof(ARCHIVE_MARKER)]{};
  const bool correctSize = file.fileSize() == sizeof(bytes);
  const bool read = correctSize && file.read(bytes, sizeof(bytes)) == static_cast<int>(sizeof(bytes));
  if (!file.close()) return ArchiveMarker::Unavailable;
  if (!correctSize) return ArchiveMarker::Damaged;
  if (!read) return ArchiveMarker::Unavailable;
  return std::memcmp(bytes, ARCHIVE_MARKER, sizeof(bytes)) == 0 ? ArchiveMarker::Valid : ArchiveMarker::Damaged;
}

enum class RolloverResult { Done, Blocked, Unavailable };
RolloverResult rolloverFullJournal() {
  uint64_t records = 0, seals = 0, receipts = 0;
  if (!Storage.lookupNumberedChildren("/.completion-journal", ".completion-", records) ||
      !Storage.lookupNumberedChildren("/.completion-journal", ".cseal-", seals) ||
      !Storage.lookupNumberedChildren("/.completion-journal", ".cretire-", receipts))
    return RolloverResult::Unavailable;
  if (records != UINT64_MAX || seals != records || receipts != records) return RolloverResult::Blocked;
  auto record = makeUniqueNoThrow<Record>();
  if (!record) return RolloverResult::Unavailable;
  size_t size = 0;
  unsigned slot = 0;
  ScanContext scan{*record, size, slot};
  scanLocked(&scan);
  if (scan.result == ScanResult::Unavailable) return RolloverResult::Unavailable;
  if (scan.result != ScanResult::Empty) return RolloverResult::Blocked;

  auto marker = inspectArchiveMarker();
  if (marker == ArchiveMarker::Missing) {
    CreateContext create{0, ".archive-", ARCHIVE_MARKER, sizeof(ARCHIVE_MARKER)};
    createLocked(&create);
    if (create.result != CreateResult::Created) return RolloverResult::Unavailable;
    marker = inspectArchiveMarker();
  }
  if (marker == ArchiveMarker::Damaged) return RolloverResult::Blocked;
  if (marker != ArchiveMarker::Valid) return RolloverResult::Unavailable;

  char archive[48];
  bool found = false;
  for (unsigned index = 0; index < 1000000; ++index) {
    snprintf(archive, sizeof(archive), "/.completion-history-%06u", index);
    const auto status = Storage.lookupPath(archive);
    if (status == HalStorage::PathStatus::Failed) return RolloverResult::Unavailable;
    if (status == HalStorage::PathStatus::Missing) {
      found = true;
      break;
    }
  }
  if (!found) return RolloverResult::Blocked;
  if (!Storage.rename("/.completion-journal", archive)) return RolloverResult::Unavailable;
  if (Storage.lookupPath("/.completion-journal") != HalStorage::PathStatus::Missing ||
      Storage.lookupPath(archive) != HalStorage::PathStatus::Present)
    return RolloverResult::Unavailable;
  if (!Storage.mkdir("/.completion-journal", false) ||
      Storage.lookupPath("/.completion-journal") != HalStorage::PathStatus::Present)
    return RolloverResult::Unavailable;
  return RolloverResult::Done;
}

bool startLocked(void* raw) {
  auto& context = *static_cast<StartContext*>(raw);
  if (Storage.lookupPath("/.completion-journal") != HalStorage::PathStatus::Present) return true;
  auto pending = makeUniqueNoThrow<Record>();
  if (!pending) return true;
  size_t pendingSize = 0;
  unsigned pendingSlot = 0;
  ScanContext scan{*pending, pendingSize, pendingSlot};
  scanLocked(&scan);
  if (scan.result == ScanResult::Candidate) {
    context.result = StartResult::NeedsRecovery;
    return true;
  }
  if (scan.result == ScanResult::Conflict || scan.result == ScanResult::Damaged) {
    context.result = StartResult::Blocked;
    return true;
  }
  if (scan.result == ScanResult::Unavailable) return true;

  std::string_view directory;
  const uint8_t *book = nullptr, *global = nullptr, *oldBook = nullptr, *oldGlobal = nullptr;
  size_t oldBookSize = 0, oldGlobalSize = 0;
  if (!decode(context.record.data(), context.size, directory, book, global) ||
      !decodeBaseline(context.record.data(), context.size, oldBook, oldBookSize, oldGlobal, oldGlobalSize) ||
      !validSnapshotPayload(book, global)) {
    context.result = StartResult::Invalid;
    return true;
  }
  // Legacy/missing canonicals need a separate checked source capture.
  if (oldBookSize != BOOK_BYTES || oldGlobalSize != GLOBAL_BYTES || !validSnapshotPayload(oldBook, oldGlobal)) {
    context.result = StartResult::Blocked;
    return true;
  }
  char bookPath[MAX_PATH + 16];
  const int pathLength =
      snprintf(bookPath, sizeof(bookPath), "%.*s/stats_v5.bin", static_cast<int>(directory.size()), directory.data());
  if (pathLength < 0 || static_cast<size_t>(pathLength) >= sizeof(bookPath)) return true;
  const auto bookMatch = compareFile(bookPath, oldBook, oldBookSize, book, BOOK_BYTES);
  const auto globalMatch = compareFile("/.crosspoint/global_stats.bin", oldGlobal, oldGlobalSize, global, GLOBAL_BYTES);
  if (bookMatch == Comparison::Unavailable || globalMatch == Comparison::Unavailable) return true;
  const bool bookBefore = bookMatch == Comparison::Before ||
                          (bookMatch == Comparison::After && std::memcmp(oldBook, book, BOOK_BYTES) == 0);
  const bool globalBefore = globalMatch == Comparison::Before ||
                            (globalMatch == Comparison::After && std::memcmp(oldGlobal, global, GLOBAL_BYTES) == 0);
  if (!bookBefore || !globalBefore) {
    context.result = StartResult::Conflict;
    return true;
  }
  uint64_t records = 0, seals = 0, receipts = 0;
  if (!Storage.lookupNumberedChildren("/.completion-journal", ".completion-", records) ||
      !Storage.lookupNumberedChildren("/.completion-journal", ".cseal-", seals) ||
      !Storage.lookupNumberedChildren("/.completion-journal", ".cretire-", receipts))
    return true;
  uint64_t occupied = records | seals | receipts;
  const auto marker = inspectArchiveMarker();
  if (marker == ArchiveMarker::Damaged) {
    context.result = StartResult::Blocked;
    return true;
  }
  if (marker == ArchiveMarker::Unavailable) return true;
  if (marker == ArchiveMarker::Valid || occupied == UINT64_MAX) {
    const auto rollover = rolloverFullJournal();
    if (rollover == RolloverResult::Blocked) context.result = StartResult::Blocked;
    if (rollover != RolloverResult::Done) return true;
    occupied = 0;
  }
  unsigned slot = 0;
  while (slot < 64 && (occupied & (uint64_t{1} << slot))) ++slot;
  if (slot == 64) {
    context.result = StartResult::Full;
    return true;
  }
  CreateContext create{slot, ".completion-", context.record.data(), context.size};
  createLocked(&create);
  if (create.result != CreateResult::Created) {
    context.result = StartResult::Uncertain;
    return true;
  }
  SealContext seal{slot, context.record, context.size};
  sealLocked(&seal);
  context.result = seal.result == CreateResult::Created ? StartResult::Started : StartResult::Uncertain;
  return true;
}
}  // namespace

CreateResult createRecord(unsigned slot, const Record& record, size_t size) {
  std::string_view directory;
  const uint8_t *book = nullptr, *global = nullptr;
  if (slot >= 64 || !decode(record.data(), size, directory, book, global)) return CreateResult::Invalid;
  CreateContext context{slot, ".completion-", record.data(), size};
  return Storage.withStorageLock(createLocked, &context) ? context.result : CreateResult::Uncertain;
}

CreateResult createSeal(unsigned slot, const Record& record, size_t size) {
  Seal seal{};
  std::string_view directory;
  const uint8_t *book = nullptr, *global = nullptr;
  if (!encodeSeal(slot, record, size, seal) || !decode(record.data(), size, directory, book, global) ||
      !validSnapshotPayload(book, global))
    return CreateResult::Invalid;
  SealContext context{slot, record, size};
  return Storage.withStorageLock(sealLocked, &context) ? context.result : CreateResult::Uncertain;
}

SealResult inspectSeal(unsigned slot, const Record& record, size_t size) {
  Seal seal{};
  if (!encodeSeal(slot, record, size, seal)) return SealResult::Invalid;
  InspectSealContext context{slot, record, size};
  return Storage.withStorageLock(inspectSealLocked, &context) ? context.result : SealResult::Unavailable;
}

InspectResult inspectRecord(unsigned slot, Record& output, size_t& size) {
  if (slot >= 64) return InspectResult::InvalidSlot;
  InspectContext context{slot, output, size};
  return Storage.withStorageLock(inspectLocked, &context) ? context.result : InspectResult::Unavailable;
}

ScanResult scanRecords(Record& output, size_t& size, unsigned& slot) {
  ScanContext context{output, size, slot};
  return Storage.withStorageLock(scanLocked, &context) ? context.result : ScanResult::Unavailable;
}

ReplayCheck checkReplay(SnapshotState& bookState, SnapshotState& globalState) {
  ReplayCheckContext context{bookState, globalState};
  return Storage.withStorageLock(checkReplayLocked, &context) ? context.result : ReplayCheck::Unavailable;
}

ReplayResult replay() {
  ReplayContext context;
  return Storage.withStorageLock(replayLocked, &context) ? context.result : ReplayResult::Unavailable;
}

RetirementResult retire() {
  RetirementContext context;
  return Storage.withStorageLock(retireLocked, &context) ? context.result : RetirementResult::Unavailable;
}

DeferredRetirementProof deferredRetirementProof(std::string_view source, uint64_t completionHash, uint8_t* policy) {
  DeferredProofContext context{source, completionHash, policy};
  return Storage.withStorageLock(deferredProofLocked, &context) ? context.result : DeferredRetirementProof::Unavailable;
}

DeferredRetirementProof retiredDeferredMoveProof(std::string_view source, uint64_t originHash, uint64_t moveHash,
                                                 RetiredJournalLocation* location) {
  DeferredProofContext context{source, moveHash, nullptr};
  context.deferredMove = true;
  context.originHash = originHash;
  context.location = location;
  return Storage.withStorageLock(deferredProofLocked, &context) ? context.result : DeferredRetirementProof::Unavailable;
}

StartResult start(const Record& record, size_t size) {
  StartContext context{record, size};
  return Storage.withStorageLock(startLocked, &context) ? context.result : StartResult::Unavailable;
}
}  // namespace readingstats::completionjournal
