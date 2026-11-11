#pragma once

#include <string_view>

#include "DeferredCompletionIntent.h"

namespace readingstats::deferredcompletion {
enum class PublishResult { Published, AlreadyPublished, NoPending, Conflict, Blocked, Full, Unavailable };
// Publishes only the matching, fully applied completion transaction. The caller
// must first apply its requested Recents action and keep the reader suspended.
// This does not retire the completion journal or authorize moving a file.
PublishResult publish(std::string_view expectedSource);
enum class FindResult { Found, Missing, Conflict, Blocked, Busy, Unavailable };
// Read-only inspection. A sealed intent alone does not prove journal retirement
// or authorize a move. Outputs change only on Found and source views borrow output.
FindResult find(std::string_view expectedSource, Record& output, size_t& size, unsigned& slot);
// Finds a sealed request, including a retired one, only when it matches the
// current pending deferred-move journal. It does not authorize finalization.
FindResult findForMoveRecovery(std::string_view expectedSource, Record& output, size_t& size, unsigned& slot);
// Also requires the matching retired completion journal in the current journal
// directory. Even Found is not proof of file or reference safety for a move.
FindResult findReady(std::string_view expectedSource, Record& output, size_t& size, unsigned& slot);
// Discovers the first sealed, unretired request and checks its retired journal.
// Found does not authorize a move. Outputs change only on Found.
FindResult discoverReady(Record& output, size_t& size, unsigned& slot);
// Prioritizes a pending deferred-move journal, otherwise discovers a ready
// request. Found identifies a candidate, not authorization to move files.
FindResult discoverResumable(Record& output, size_t& size, unsigned& slot);
}  // namespace readingstats::deferredcompletion
