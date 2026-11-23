#pragma once

#include <cstdint>
#include <string_view>

#include "network/ProtectedPaths.h"

namespace bookmove {

struct DeletionPath {
  std::string_view original;
  uint64_t transactionId = 0;
};

inline constexpr std::string_view HELD_SUFFIX = ".held";

// The first half of the event ID names the archived move journal. The plan in
// that journal must match the entire held path, including the other half.
inline bool parseDeletionPath(std::string_view held, DeletionPath& result) {
  constexpr auto root = protectedpaths::BOOK_DELETION_QUEUE;
  constexpr size_t idStart = root.size() + 1;
  constexpr size_t originalStart = idStart + 32;
  if (held.size() <= originalStart + HELD_SUFFIX.size() + 1 || !held.starts_with(root) || held[root.size()] != '/' ||
      held[originalStart] != '/' || !held.ends_with(HELD_SUFFIX)) {
    return false;
  }
  uint64_t transactionId = 0;
  for (size_t i = idStart; i < originalStart; ++i) {
    const char ch = held[i];
    const auto digit = std::string_view("0123456789abcdef").find(ch);
    if (digit == std::string_view::npos) return false;
    if (i < idStart + 16) transactionId = (transactionId << 4) | digit;
  }
  if (transactionId == 0) return false;
  result = {held.substr(originalStart, held.size() - originalStart - HELD_SUFFIX.size()), transactionId};
  return true;
}

}  // namespace bookmove
