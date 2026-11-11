#pragma once

#include "CompletionJournal.h"

namespace readingstats::completionjournal {
// A created record is not a committed or replayed transaction. In particular,
// Uncertain requires preserving the slot until recovery can inspect it.
enum class CreateResult { Created, Occupied, Invalid, Uncertain };
CreateResult createRecord(unsigned slot, const Record& record, size_t size);
// Create the slot's separate seal only after re-reading the exact prepared
// record. Uncertain preserves all files and requires inspection before retry.
CreateResult createSeal(unsigned slot, const Record& record, size_t size);
enum class SealResult { Missing, Matching, Damaged, Invalid, Unavailable };
// A matching seal is only an observation; a transaction owner must keep the
// storage lock while checking the record and deciding how to proceed.
SealResult inspectSeal(unsigned slot, const Record& record, size_t size);
// Valid means the envelope passes structural and checksum checks, not that its
// snapshots are semantically valid or that the transaction is committed.
enum class InspectResult { Missing, Valid, Damaged, InvalidSlot, Unavailable };
// On anything other than Valid, output and size are left untouched.
InspectResult inspectRecord(unsigned slot, Record& output, size_t& size);
// A candidate has a matching seal, but its statistics still need semantic
// validation. PreparedOnly records have no seal and must be preserved.
enum class ScanResult { Empty, PreparedOnly, Candidate, Conflict, Damaged, Unavailable };
// Outputs change only for Candidate. The scan runs under the storage lock.
ScanResult scanRecords(Record& output, size_t& size, unsigned& slot);
enum class SnapshotState { Before, After };
enum class ReplayCheck { NoCandidate, Ready, Conflict, Blocked, Unavailable };
// Read-only baseline check. Ready does not establish ownership, verify backup
// clusters, or authorize writing. Actual replay must repeat all checks under
// the same storage lock as its writes. Outputs change only on Ready.
ReplayCheck checkReplay(SnapshotState& bookState, SnapshotState& globalState);
// Experimental recovery: journals are deliberately retained after applying.
// No ordinary stats reader or writer is coordinated with this operation yet.
enum class ReplayResult { NoCandidate, AppliedUnretired, Conflict, Blocked, Unavailable, Incomplete };
ReplayResult replay();
enum class RetirementResult { NoCandidate, Retired, NotApplied, Conflict, Blocked, Unavailable, Uncertain };
// Writes a retirement receipt only if both files still match the journal's
// intended snapshots. All transaction files remain on storage.
RetirementResult retire();
enum class DeferredRetirementProof { Retired, Missing, Busy, Conflict, Damaged, Unavailable };
// Checks the current journal and inventoried, fully sealed archives. A match
// is an observation, not move authorization.
DeferredRetirementProof deferredRetirementProof(std::string_view source, uint64_t completionHash,
                                                uint8_t* policy = nullptr);
struct RetiredJournalLocation {
  std::array<char, 48> directory{};
  unsigned slot = 0;
};
// Finds one exact retired deferred-move journal among inventoried archives,
// refusing duplicates. It does not verify files or raw-card concurrency.
DeferredRetirementProof retiredDeferredMoveProof(std::string_view source, uint64_t originHash, uint64_t moveHash,
                                                 RetiredJournalLocation* location = nullptr);
enum class StartResult { Started, NeedsRecovery, Conflict, Blocked, Full, Invalid, Unavailable, Uncertain };
// Experimental start: checks the current canonical snapshots against the
// record's baseline and creates one record and seal under the storage lock.
// The caller must stop all uncoordinated stats writers before using this.
StartResult start(const Record& record, size_t size);
}  // namespace readingstats::completionjournal
