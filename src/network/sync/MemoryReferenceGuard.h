#pragma once
#include <string_view>

#include "StoredReferenceGuard.h"

namespace syncpublish {
// Call only while the caller owns exclusive access to the state and the recent
// collection. The comparison does not acquire locks or reserve the destination.
template <typename RecentBooks>
bool memoryReferencesClear(std::string_view openPath, const RecentBooks& recents, std::string_view destination) {
  if (referenceMayConflict(openPath, destination)) return false;
  for (const auto& book : recents)
    if (referenceMayConflict(book.path, destination)) return false;
  return true;
}
}  // namespace syncpublish
