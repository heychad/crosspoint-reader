#pragma once

#include <cstdint>
#include <string>

#include "CompletionJournal.h"

struct ReadingStatsDate;

namespace readingstats {
// The caller handles Recents; deferred moves are recorded but not performed.
// The caller must ensure this book has no active reader and coordinate every
// statistics reader and writer before enabling this operation in the UI.
enum class ClosedCompletionResult {
  Applied,
  Unchanged,
  Invalid,
  Busy,
  Unavailable,
  RecoveryRequired,
  UnsupportedMoveSource
};
// Matches the firmware's current path-derived cache identity for this book.
bool completionCacheMatchesBook(const std::string& bookPath, const std::string& cache);
ClosedCompletionResult setClosedBookCompletionStats(const std::string& bookPath, bool completed,
                                                    const ReadingStatsDate& today, uint8_t policy = 0);
// Bind the move journal to the exact queued request that authorized it.
ClosedCompletionResult setClosedBookDeferredMoveStats(const std::string& bookPath, const ReadingStatsDate& today,
                                                      uint64_t originHash, bool removeRecent);
// Start a recoverable book/global update with exact book snapshots. The caller
// must preserve the prior book snapshot first and finish or recover the journal.
ClosedCompletionResult restoreClosedBookSnapshot(const std::string& bookPath, const std::string& expectedCache,
                                                 const completionjournal::Book& expectedCurrent,
                                                 const completionjournal::Book& replacement);
enum class CompletionRecoveryResult { Recovered, NoPending, Busy, DifferentTransaction, ManualReview, Unavailable };
// Retry only the pending completion for this book and target; never erase a
// damaged or conflicting journal. Caller must coordinate all stats writers.
CompletionRecoveryResult recoverClosedBookCompletionStats(const std::string& bookPath, bool completed);
enum class CompletionFinalizeResult { Finalized, NoPending, Busy, DifferentTransaction, ManualReview, Unavailable };
// Call only after the completion's requested organization actions have succeeded.
// Deferred moves are published before retirement; immediate moves cannot yet
// be finalized because durable proof of migrated references is not implemented.
CompletionFinalizeResult finalizeClosedBookCompletion(const std::string& bookPath, bool completed);
enum class CompletionProbeResult { Matching, NoPending, DifferentTransaction, ManualReview, Unavailable };
// Read-only discovery for a reopened book's recovery control. The target is
// changed only on Matching; recovery still rechecks everything before writing.
CompletionProbeResult probeClosedBookCompletion(const std::string& bookPath, bool& target, uint8_t* policy = nullptr);
}  // namespace readingstats
