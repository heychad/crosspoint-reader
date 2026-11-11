#include "DeferredCompletionMove.h"

#include <HalStorage.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

#include "BookReadingStats.h"
#include "CompletionJournalIO.h"
#include "CompletionMovePlanIO.h"
#include "CompletionTransaction.h"
#include "DeferredCompletionIntentIO.h"
#include "ReadingStatsStore.h"

namespace readingstats::deferredcompletion {
namespace {
struct Context {
  const std::string& source;
  MoveAssessment result = MoveAssessment::Unavailable;
};

bool assessLocked(void* raw) {
  auto& context = *static_cast<Context*>(raw);
  auto record = makeUniqueNoThrow<Record>();
  if (!record) return true;
  size_t size = 0;
  unsigned slot = 0;
  switch (findReady(context.source, *record, size, slot)) {
    case FindResult::Missing:
      context.result = MoveAssessment::Missing;
      return true;
    case FindResult::Busy:
      context.result = MoveAssessment::Busy;
      return true;
    case FindResult::Conflict:
    case FindResult::Blocked:
      context.result = MoveAssessment::Blocked;
      return true;
    case FindResult::Unavailable:
      return true;
    case FindResult::Found:
      break;
  }
  const auto sourceStatus = Storage.lookupPath(context.source.c_str());
  if (sourceStatus == HalStorage::PathStatus::Failed) return true;
  if (sourceStatus != HalStorage::PathStatus::Present) {
    context.result = MoveAssessment::Blocked;
    return true;
  }
  HalFile bookFile;
  if (!Storage.openFileForRead("STATS", context.source, bookFile)) return true;
  const bool regular = !bookFile.isDirectory();
  if (!bookFile.close()) return true;
  if (!regular) {
    context.result = MoveAssessment::Blocked;
    return true;
  }
  const std::string cache = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(context.source));
  if (!completionCacheMatchesBook(context.source, cache)) {
    context.result = MoveAssessment::Blocked;
    return true;
  }
  const std::string statsPath = bookDirectory(cache) + "/stats_v5.bin";
  const auto tempStatus = Storage.lookupPath((statsPath + ".tmp").c_str());
  if (tempStatus == HalStorage::PathStatus::Failed) return true;
  if (tempStatus == HalStorage::PathStatus::Present) {
    context.result = MoveAssessment::Blocked;
    return true;
  }
  const auto statsStatus = Storage.lookupPath(statsPath.c_str());
  if (statsStatus == HalStorage::PathStatus::Failed) return true;
  if (statsStatus != HalStorage::PathStatus::Present) {
    context.result = MoveAssessment::Blocked;
    return true;
  }
  HalFile file;
  if (!Storage.openFileForRead("STATS", statsPath, file)) return true;
  uint8_t bytes[BookReadingStats::CURRENT_FILE_SIZE]{};
  const bool correctSize = file.fileSize() == sizeof(bytes);
  const bool read = correctSize && file.read(bytes, sizeof(bytes)) == static_cast<int>(sizeof(bytes));
  if (!file.close()) return true;
  if (!correctSize) {
    context.result = MoveAssessment::Blocked;
    return true;
  }
  if (!read) return true;
  auto stats = makeUniqueNoThrow<BookReadingStats>();
  if (!stats) return true;
  if (!BookReadingStats::decode(bytes, sizeof(bytes), *stats)) {
    context.result = MoveAssessment::Blocked;
    return true;
  }
  context.result = stats->isCompleted ? MoveAssessment::Ready : MoveAssessment::Unfinished;
  return true;
}

struct CancelContext {
  const std::string& source;
  CancelResult result = CancelResult::Unavailable;
};

bool cancelLocked(void* raw) {
  auto& context = *static_cast<CancelContext*>(raw);
  switch (assess(context.source)) {
    case MoveAssessment::Missing:
      context.result = CancelResult::Missing;
      return true;
    case MoveAssessment::Ready:
      context.result = CancelResult::StillFinished;
      return true;
    case MoveAssessment::Busy:
      context.result = CancelResult::Busy;
      return true;
    case MoveAssessment::Blocked:
      context.result = CancelResult::Blocked;
      return true;
    case MoveAssessment::Unavailable:
      return true;
    case MoveAssessment::Unfinished:
      break;
  }
  auto record = makeUniqueNoThrow<Record>();
  if (!record) return true;
  size_t size = 0;
  unsigned slot = 0;
  if (findReady(context.source, *record, size, slot) != FindResult::Found) return true;
  Seal receipt{};
  if (!encodeSeal(slot, *record, size, receipt, true)) return true;
  char path[64];
  snprintf(path, sizeof(path), "/.completion-deferred/.dretire-%02u", slot);
  if (Storage.lookupPath(path) != HalStorage::PathStatus::Missing) return true;
  HalFile file = Storage.openChecked(path, O_WRONLY | O_CREAT | O_EXCL);
  if (!file) return true;
  const bool written = file.write(receipt.data(), receipt.size()) == receipt.size();
  const bool synced = file.sync();
  const bool closed = file.close();
  if (!written || !synced || !closed) return true;
  HalFile check;
  if (!Storage.openFileForRead("STATS", path, check)) return true;
  Seal actual{};
  const bool correctSize = check.fileSize() == actual.size();
  const bool read = correctSize && check.read(actual.data(), actual.size()) == static_cast<int>(actual.size());
  if (!check.close() || !read || std::memcmp(actual.data(), receipt.data(), receipt.size()) != 0) return true;
  context.result = CancelResult::Cancelled;
  return true;
}

struct PrepareContext {
  const std::string& source;
  const std::string& destination;
  const ReadingStatsDate& today;
  PrepareMoveResult result = PrepareMoveResult::Unavailable;
};

enum class ResumeMove { NoPending, Prepared, Busy, RecoveryRequired, Unavailable };
ResumeMove resumePreparedMove(const std::string& source, const std::string& destination) {
  const auto journalStatus = Storage.lookupPath("/.completion-journal");
  if (journalStatus == HalStorage::PathStatus::Missing) return ResumeMove::NoPending;
  if (journalStatus != HalStorage::PathStatus::Present) return ResumeMove::Unavailable;
  auto journal = makeUniqueNoThrow<completionjournal::Record>();
  if (!journal) return ResumeMove::Unavailable;
  size_t journalSize = 0;
  unsigned journalSlot = 0;
  const auto state = completionjournal::scanRecords(*journal, journalSize, journalSlot);
  if (state == completionjournal::ScanResult::Empty || state == completionjournal::ScanResult::PreparedOnly)
    return ResumeMove::NoPending;
  if (state == completionjournal::ScanResult::Conflict || state == completionjournal::ScanResult::Damaged)
    return ResumeMove::RecoveryRequired;
  if (state != completionjournal::ScanResult::Candidate) return ResumeMove::Unavailable;
  std::string_view journalSource;
  uint64_t journalOrigin = 0;
  if (!completionjournal::decodeDeferredMove(journal->data(), journalSize, journalSource, journalOrigin) ||
      journalSource != source)
    return ResumeMove::Busy;
  auto intent = makeUniqueNoThrow<Record>();
  if (!intent) return ResumeMove::Unavailable;
  size_t intentSize = 0;
  unsigned intentSlot = 0;
  const auto found = findForMoveRecovery(source, *intent, intentSize, intentSlot);
  if (found == FindResult::Busy) return ResumeMove::Busy;
  if (found != FindResult::Found) return ResumeMove::RecoveryRequired;
  std::string_view intentSource;
  uint64_t intentOrigin = 0;
  if (!decodeWithHash(intent->data(), intentSize, intentOrigin, intentSource) || intentSource != source ||
      intentOrigin != journalOrigin)
    return ResumeMove::RecoveryRequired;
  if (recoverClosedBookCompletionStats(source, true) != CompletionRecoveryResult::Recovered)
    return ResumeMove::RecoveryRequired;
  if (completionmove::preparePlan(source, destination) != completionmove::PlanResult::Ready)
    return ResumeMove::RecoveryRequired;
  return ResumeMove::Prepared;
}

bool prepareLocked(void* raw) {
  auto& context = *static_cast<PrepareContext*>(raw);
  if (!completionmove::validPath(context.destination) || !context.destination.starts_with("/read/") ||
      context.destination.find('/', 6) != std::string::npos || context.source == context.destination) {
    context.result = PrepareMoveResult::Blocked;
    return true;
  }
  switch (resumePreparedMove(context.source, context.destination)) {
    case ResumeMove::Prepared:
      context.result = PrepareMoveResult::Prepared;
      return true;
    case ResumeMove::Busy:
      context.result = PrepareMoveResult::Busy;
      return true;
    case ResumeMove::RecoveryRequired:
      context.result = PrepareMoveResult::RecoveryRequired;
      return true;
    case ResumeMove::Unavailable:
      return true;
    case ResumeMove::NoPending:
      break;
  }
  switch (assess(context.source)) {
    case MoveAssessment::Missing:
      context.result = PrepareMoveResult::Missing;
      return true;
    case MoveAssessment::Busy:
      context.result = PrepareMoveResult::Busy;
      return true;
    case MoveAssessment::Blocked:
      context.result = PrepareMoveResult::Blocked;
      return true;
    case MoveAssessment::Unavailable:
      return true;
    case MoveAssessment::Unfinished: {
      const auto cancelled = cancelIfUnfinished(context.source);
      if (cancelled == CancelResult::Cancelled || cancelled == CancelResult::Missing)
        context.result = PrepareMoveResult::Cancelled;
      else if (cancelled == CancelResult::Busy)
        context.result = PrepareMoveResult::Busy;
      else if (cancelled == CancelResult::Blocked)
        context.result = PrepareMoveResult::Blocked;
      return true;
    }
    case MoveAssessment::Ready:
      break;
  }
  const auto folderStatus = Storage.lookupPath("/read");
  if (folderStatus == HalStorage::PathStatus::Failed) return true;
  if (folderStatus == HalStorage::PathStatus::Missing &&
      (!Storage.mkdir("/read", false) || Storage.lookupPath("/read") != HalStorage::PathStatus::Present))
    return true;
  HalFile folder = Storage.open("/read");
  if (!folder) return true;
  const bool isDirectory = folder.isDirectory();
  if (!folder.close()) return true;
  if (!isDirectory) {
    context.result = PrepareMoveResult::Blocked;
    return true;
  }
  const auto destinationStatus = Storage.lookupPath(context.destination.c_str());
  if (destinationStatus == HalStorage::PathStatus::Failed) return true;
  if (destinationStatus == HalStorage::PathStatus::Present) {
    context.result = PrepareMoveResult::Blocked;
    return true;
  }
  auto record = makeUniqueNoThrow<Record>();
  if (!record) return true;
  size_t size = 0;
  unsigned slot = 0;
  if (findReady(context.source, *record, size, slot) != FindResult::Found) return true;
  uint64_t completionHash = 0;
  std::string_view storedSource;
  if (!decodeWithHash(record->data(), size, completionHash, storedSource) || storedSource != context.source)
    return true;
  uint8_t originalPolicy = 0;
  if (completionjournal::deferredRetirementProof(storedSource, completionHash, &originalPolicy) !=
      completionjournal::DeferredRetirementProof::Retired)
    return true;
  const auto started = setClosedBookDeferredMoveStats(context.source, context.today, completionHash,
                                                      originalPolicy & completionjournal::REMOVE_RECENTS);
  if (started == ClosedCompletionResult::Busy) {
    context.result = PrepareMoveResult::Busy;
    return true;
  }
  if (started == ClosedCompletionResult::RecoveryRequired) {
    context.result = PrepareMoveResult::RecoveryRequired;
    return true;
  }
  if (started != ClosedCompletionResult::Applied) return true;
  if (completionmove::preparePlan(context.source, context.destination) != completionmove::PlanResult::Ready) {
    context.result = PrepareMoveResult::RecoveryRequired;
    return true;
  }
  context.result = PrepareMoveResult::Prepared;
  return true;
}

}  // namespace

MoveAssessment assess(const std::string& source) {
  Context context{source};
  return Storage.withStorageLock(assessLocked, &context) ? context.result : MoveAssessment::Unavailable;
}

CancelResult cancelIfUnfinished(const std::string& source) {
  CancelContext context{source};
  return Storage.withStorageLock(cancelLocked, &context) ? context.result : CancelResult::Unavailable;
}

PrepareMoveResult prepareMove(const std::string& source, const std::string& destination,
                              const ReadingStatsDate& today) {
  PrepareContext context{source, destination, today};
  return Storage.withStorageLock(prepareLocked, &context) ? context.result : PrepareMoveResult::Unavailable;
}

}  // namespace readingstats::deferredcompletion
