#pragma once

#include <string>

namespace bookmove {
enum class Result { Moved, NothingPending, Invalid, Conflict, Busy, Unavailable, RecoveryRequired, InsufficientSpace };
bool managedBook(const std::string& path);
// True also on an unreadable journal path. Never authorizes an edit.
bool pending();
// Main task only, with all live readers closed. The owner takes render then
// storage locks, journals identities, and renames the book without copying it.
Result move(const std::string& from, const std::string& to);
Result recover();
}  // namespace bookmove
