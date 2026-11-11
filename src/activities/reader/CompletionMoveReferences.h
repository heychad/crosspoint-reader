#pragma once

namespace readingstats::completionmove {
enum class ReferenceResult { Updated, NoPlan, Conflict, Unavailable, RecoveryRequired };
// Complete the planned file moves, then update Recents and the app reopen
// Requires the activity manager's main task. Acquires the render lock before
// storage, updates and rechecks references, then retires the request and
// journal. Other memory writers are not yet fully coordinated.
ReferenceResult updateMoveReferences();
enum class DeferredRecoveryResult { Recovered, NoPending, Busy, RecoveryRequired, Unavailable };
// Finish an interrupted deferred-completion transaction before moving files.
// Requires the main task, with no render lock held by the caller.
DeferredRecoveryResult recoverDeferredCompletion();
}  // namespace readingstats::completionmove
