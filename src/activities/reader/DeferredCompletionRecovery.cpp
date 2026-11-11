#include <HalStorage.h>
#include <Memory.h>

#include <string>

#include "CompletionJournalIO.h"
#include "CompletionMoveReferences.h"
#include "CompletionTransaction.h"
#include "RecentBooksStore.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"

namespace readingstats::completionmove {
namespace {
DeferredRecoveryResult recoverDeferredLocked() {
  const auto status = Storage.lookupPath("/.completion-journal");
  if (status == HalStorage::PathStatus::Missing) return DeferredRecoveryResult::NoPending;
  if (status != HalStorage::PathStatus::Present) return DeferredRecoveryResult::Unavailable;
  auto record = makeUniqueNoThrow<completionjournal::Record>();
  if (!record) return DeferredRecoveryResult::Unavailable;
  size_t size = 0;
  unsigned slot = 0;
  switch (completionjournal::scanRecords(*record, size, slot)) {
    case completionjournal::ScanResult::Empty:
    case completionjournal::ScanResult::PreparedOnly:
      return DeferredRecoveryResult::NoPending;
    case completionjournal::ScanResult::Conflict:
    case completionjournal::ScanResult::Damaged:
      return DeferredRecoveryResult::RecoveryRequired;
    case completionjournal::ScanResult::Unavailable:
      return DeferredRecoveryResult::Unavailable;
    case completionjournal::ScanResult::Candidate:
      break;
  }
  const uint8_t policy = (*record)[9];
  if (policy & completionjournal::MOVE_FROM_DEFERRED) return DeferredRecoveryResult::NoPending;
  if (!(policy & completionjournal::DEFER_MOVE_TO_READ)) return DeferredRecoveryResult::Busy;
  std::string_view source;
  if (!completionjournal::decodeDeferredSource(record->data(), size, source))
    return DeferredRecoveryResult::RecoveryRequired;
  const std::string path(source);
  record.reset();
  const auto recovered = recoverClosedBookCompletionStats(path, true);
  if (recovered == CompletionRecoveryResult::Busy) return DeferredRecoveryResult::Busy;
  if (recovered == CompletionRecoveryResult::Unavailable) return DeferredRecoveryResult::Unavailable;
  if (recovered != CompletionRecoveryResult::Recovered) return DeferredRecoveryResult::RecoveryRequired;
  if ((policy & completionjournal::REMOVE_RECENTS) && !RECENT_BOOKS.removeByPathPersisted(path))
    return DeferredRecoveryResult::RecoveryRequired;
  return finalizeClosedBookCompletion(path, true) == CompletionFinalizeResult::Finalized
             ? DeferredRecoveryResult::Recovered
             : DeferredRecoveryResult::RecoveryRequired;
}

bool dispatchDeferredRecovery(void* raw) {
  *static_cast<DeferredRecoveryResult*>(raw) = recoverDeferredLocked();
  return true;
}

}  // namespace

DeferredRecoveryResult recoverDeferredCompletion() {
  if (!activityManager.isMainTask()) return DeferredRecoveryResult::Unavailable;
  RenderLock render(RenderLock::Mode::Try);
  if (!render.ownsLock()) return DeferredRecoveryResult::Unavailable;
  DeferredRecoveryResult result = DeferredRecoveryResult::Unavailable;
  return Storage.withStorageLock(dispatchDeferredRecovery, &result) ? result : DeferredRecoveryResult::Unavailable;
}

}  // namespace readingstats::completionmove
