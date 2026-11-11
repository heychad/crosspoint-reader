#pragma once

#include <string>

struct ReadingStatsDate;

namespace readingstats::deferredcompletion {
enum class MoveAssessment { Ready, Unfinished, Missing, Busy, Blocked, Unavailable };
// Read-only check while no book is open. Ready does not authorize file moves;
// the executor must repeat checks under the same lock as the move transaction.
MoveAssessment assess(const std::string& source);
enum class CancelResult { Cancelled, Missing, StillFinished, Busy, Blocked, Unavailable };
// Retire a request only while its book is demonstrably unfinished. Files and
// statistics remain in place; a damaged receipt is preserved for recovery.
CancelResult cancelIfUnfinished(const std::string& source);
enum class PrepareMoveResult { Prepared, Cancelled, Missing, Busy, Blocked, RecoveryRequired, Unavailable };
// Begins the closed-book move transaction and seals its plan. The book is not
// moved here; after an incomplete transaction the caller must recover it.
PrepareMoveResult prepareMove(const std::string& source, const std::string& destination, const ReadingStatsDate& today);
enum class ExecuteMoveResult { Moved, Cancelled, Missing, Busy, Blocked, RecoveryRequired, Unavailable };
// Run only after the reader and its lease are destroyed, on the main task and
// without the render lock. Missing does not prove a previous move succeeded.
ExecuteMoveResult executeAfterReaderClosed(const std::string& source, const ReadingStatsDate& today);
// One bounded discovery and execution attempt, after reader teardown.
using BeforeDeferredMove = void (*)(void*);
ExecuteMoveResult executeOnePending(const ReadingStatsDate& today, BeforeDeferredMove beforeMove = nullptr,
                                    void* context = nullptr);
enum class RetireMoveResult { Retired, AlreadyRetired, Missing, Busy, RecoveryRequired, Unavailable };
// Retires only the request after its sealed move plan's files are verified.
// The completion journal remains pending until references are finalized.
RetireMoveResult retireMovedIntent(const std::string& source);
}  // namespace readingstats::deferredcompletion
