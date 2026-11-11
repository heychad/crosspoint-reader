#include "CompletionMoveReferences.h"

#include <HalStorage.h>
#include <Memory.h>

#include <functional>
#include <string>

#include "CompletionJournal.h"
#include "CompletionJournalIO.h"
#include "CompletionMovePlanIO.h"
#include "CompletionTransaction.h"
#include "CrossPointState.h"
#include "DeferredCompletionMove.h"
#include "RecentBooksStore.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "network/sync/StoredReferenceGuard.h"

namespace readingstats::completionmove {
namespace {
ReferenceResult updateMoveReferencesLocked() {
  const auto moved = movePreparedFiles();
  if (moved == MoveFilesResult::NoPlan) return ReferenceResult::NoPlan;
  if (moved == MoveFilesResult::Unavailable) return ReferenceResult::Unavailable;
  if (moved == MoveFilesResult::Conflict) return ReferenceResult::Conflict;
  if (moved != MoveFilesResult::Moved) return ReferenceResult::RecoveryRequired;
  const auto verified = verifyPreparedFiles();
  if (verified == VerifyFilesResult::NoPlan) return ReferenceResult::NoPlan;
  if (verified == VerifyFilesResult::Unavailable) return ReferenceResult::Unavailable;
  if (verified != VerifyFilesResult::ContentMatches) return ReferenceResult::RecoveryRequired;
  auto plan = makeUniqueNoThrow<Plan>();
  if (!plan) return ReferenceResult::Unavailable;
  size_t size = 0;
  std::string_view from, to;
  if (inspectPlan(*plan, size, from, to) != PlanResult::Ready) return ReferenceResult::RecoveryRequired;
  const std::string oldPath(from), newPath(to);
  plan.reset();
  bool target = false;
  uint8_t policy = 0;
  if (probeClosedBookCompletion(newPath, target, &policy) != CompletionProbeResult::Matching || !target ||
      !(policy & completionjournal::MOVE_TO_READ))
    return ReferenceResult::RecoveryRequired;
  if (policy & completionjournal::REMOVE_RECENTS) {
    if (!RECENT_BOOKS.removeByPathPersisted(oldPath)) return ReferenceResult::RecoveryRequired;
  } else {
    // Generated cover cache is not moved. Keep its old path until a separate
    // cache-cleanup policy can regenerate or transfer it safely.
    const auto oldCache = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(oldPath));
    if (!RECENT_BOOKS.updatePathPersisted(oldPath, newPath, oldCache, oldCache))
      return ReferenceResult::RecoveryRequired;
  }
  if (!APP_STATE.updateOpenEpubPathPersisted(oldPath, newPath)) return ReferenceResult::RecoveryRequired;
  const auto referencesClear = [&]() {
    const auto proceed = [](void*) { return true; };
    if (syncpublish::inspectStoredSourceReferences(Storage, oldPath, proceed, nullptr) !=
        syncpublish::ReferenceGuardResult::Clear)
      return false;
    if (syncpublish::referenceMayReferTo(APP_STATE.openEpubPath, oldPath)) return false;
    for (const auto& book : RECENT_BOOKS.getBooks())
      if (syncpublish::referenceMayReferTo(book.path, oldPath)) return false;
    return true;
  };
  if (!referencesClear()) return ReferenceResult::RecoveryRequired;
  if (policy & completionjournal::MOVE_FROM_DEFERRED) {
    const auto retired = deferredcompletion::retireMovedIntent(oldPath);
    if (retired == deferredcompletion::RetireMoveResult::Unavailable) return ReferenceResult::Unavailable;
    if (retired != deferredcompletion::RetireMoveResult::Retired &&
        retired != deferredcompletion::RetireMoveResult::AlreadyRetired)
      return ReferenceResult::RecoveryRequired;
  }
  const auto finalCheck = verifyPreparedFiles();
  if (finalCheck == VerifyFilesResult::Unavailable) return ReferenceResult::Unavailable;
  if (finalCheck != VerifyFilesResult::ContentMatches || !referencesClear()) return ReferenceResult::RecoveryRequired;
  const auto finalized = completionjournal::retire();
  if (finalized == completionjournal::RetirementResult::Unavailable) return ReferenceResult::Unavailable;
  return finalized == completionjournal::RetirementResult::Retired ? ReferenceResult::Updated
                                                                   : ReferenceResult::RecoveryRequired;
}

bool dispatchMoveReferences(void* raw) {
  *static_cast<ReferenceResult*>(raw) = updateMoveReferencesLocked();
  return true;
}
}  // namespace

ReferenceResult updateMoveReferences() {
  if (!activityManager.isMainTask()) return ReferenceResult::Unavailable;
  RenderLock render(RenderLock::Mode::Try);
  if (!render.ownsLock()) return ReferenceResult::Unavailable;
  ReferenceResult result = ReferenceResult::Unavailable;
  return Storage.withStorageLock(dispatchMoveReferences, &result) ? result : ReferenceResult::Unavailable;
}
}  // namespace readingstats::completionmove
