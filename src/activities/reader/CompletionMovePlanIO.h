#pragma once

#include "CompletionMovePlan.h"

namespace readingstats::completionmove {
enum class PlanResult { Ready, Missing, Conflict, Damaged, Invalid, Unavailable, Uncertain };

// Book, protected sidecars, five clipping-store files, and three native bookmark
// files, persistent native state, legacy bookmarks, and persistent legacy state.
bool itemPaths(std::string_view from, std::string_view to, size_t index, std::string& source, std::string& target);

// Publishes an immutable plan and a separate seal for the current matching
// completion. This prepares no book move; callers must still preflight all
// sidecars and coordinate writers before making any changes.
PlanResult preparePlan(std::string_view from, std::string_view to);
// Retrieves the plan only when it and its seal match the current completion.
// Output arguments are changed only on Ready and borrow the supplied buffer.
PlanResult inspectPlan(Plan& output, size_t& size, std::string_view& from, std::string_view& to,
                       FileIdentity* identities = nullptr);
enum class RetiredPlanResult { Ready, Missing, Busy, Conflict, Damaged, Unavailable };
// Observes one retired journal's sealed plan and its exact paths. It does not
// establish uniqueness, verify files or references, or authorize cleanup.
RetiredPlanResult inspectRetiredPlan(std::string_view source, std::string_view destination, uint64_t originHash,
                                     uint64_t moveHash, Plan* output = nullptr, size_t* outputSize = nullptr);
enum class MoveFilesResult { Moved, NoPlan, Conflict, Unavailable, RecoveryRequired };
// Copies persistent reading-state files, then moves the remaining files.
// Does not migrate generated cache data, Recents or app state and must not
// retire the transaction.
MoveFilesResult movePreparedFiles();
// Checks moved files and copied state, including pairwise allocation chains.
// It does not establish full-volume ownership, verify references, or authorize
// journal retirement.
enum class VerifyFilesResult { ContentMatches, NoPlan, NotReady, Unavailable };
VerifyFilesResult verifyPreparedFiles();
// Read-only comparison against one retired plan. This is not proof of unique
// transaction identity, complete filesystem ownership, or safe finalization.
VerifyFilesResult verifyRetiredFiles(std::string_view source, std::string_view destination, uint64_t originHash,
                                     uint64_t moveHash);
}  // namespace readingstats::completionmove
