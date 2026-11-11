#include <Memory.h>

#include "CompletionMovePlan.h"
#include "CompletionMoveReferences.h"
#include "DeferredCompletionIntentIO.h"
#include "DeferredCompletionMove.h"
#include "activities/ActivityManager.h"
#include "util/ReadFolder.h"

namespace readingstats::deferredcompletion {
ExecuteMoveResult executeAfterReaderClosed(const std::string& source, const ReadingStatsDate& today) {
  if (!activityManager.isMainTask()) return ExecuteMoveResult::Unavailable;
  if (!completionmove::validPath(source) || isInReadFolder(source)) return ExecuteMoveResult::Blocked;
  const auto slash = source.find_last_of('/');
  if (slash == std::string::npos || slash + 1 == source.size()) return ExecuteMoveResult::Blocked;
  const std::string destination = "/read/" + source.substr(slash + 1);
  switch (prepareMove(source, destination, today)) {
    case PrepareMoveResult::Cancelled:
      return ExecuteMoveResult::Cancelled;
    case PrepareMoveResult::Missing:
      return ExecuteMoveResult::Missing;
    case PrepareMoveResult::Busy:
      return ExecuteMoveResult::Busy;
    case PrepareMoveResult::Blocked:
      return ExecuteMoveResult::Blocked;
    case PrepareMoveResult::RecoveryRequired:
      return ExecuteMoveResult::RecoveryRequired;
    case PrepareMoveResult::Unavailable:
      return ExecuteMoveResult::Unavailable;
    case PrepareMoveResult::Prepared:
      break;
  }
  switch (completionmove::updateMoveReferences()) {
    case completionmove::ReferenceResult::Updated:
      return ExecuteMoveResult::Moved;
    case completionmove::ReferenceResult::Unavailable:
      return ExecuteMoveResult::Unavailable;
    case completionmove::ReferenceResult::NoPlan:
    case completionmove::ReferenceResult::Conflict:
    case completionmove::ReferenceResult::RecoveryRequired:
      return ExecuteMoveResult::RecoveryRequired;
  }
  return ExecuteMoveResult::RecoveryRequired;
}

ExecuteMoveResult executeOnePending(const ReadingStatsDate& today, BeforeDeferredMove beforeMove, void* context) {
  if (!activityManager.isMainTask()) return ExecuteMoveResult::Unavailable;
  switch (completionmove::recoverDeferredCompletion()) {
    case completionmove::DeferredRecoveryResult::Recovered:
    case completionmove::DeferredRecoveryResult::NoPending:
      break;
    case completionmove::DeferredRecoveryResult::Busy:
      return ExecuteMoveResult::Busy;
    case completionmove::DeferredRecoveryResult::RecoveryRequired:
      return ExecuteMoveResult::RecoveryRequired;
    case completionmove::DeferredRecoveryResult::Unavailable:
      return ExecuteMoveResult::Unavailable;
  }
  auto record = makeUniqueNoThrow<Record>();
  if (!record) return ExecuteMoveResult::Unavailable;
  size_t size = 0;
  unsigned slot = 0;
  switch (discoverResumable(*record, size, slot)) {
    case FindResult::Missing:
      return ExecuteMoveResult::Missing;
    case FindResult::Busy:
      return ExecuteMoveResult::Busy;
    case FindResult::Conflict:
    case FindResult::Blocked:
      return ExecuteMoveResult::RecoveryRequired;
    case FindResult::Unavailable:
      return ExecuteMoveResult::Unavailable;
    case FindResult::Found:
      break;
  }
  uint64_t hash = 0;
  std::string_view source;
  if (!decodeWithHash(record->data(), size, hash, source)) return ExecuteMoveResult::RecoveryRequired;
  const std::string path(source);
  record.reset();
  if (beforeMove) beforeMove(context);
  return executeAfterReaderClosed(path, today);
}
}  // namespace readingstats::deferredcompletion
