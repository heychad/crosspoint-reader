#pragma once

#include <CheckedStoragePath.h>

#include <string_view>

namespace syncpublish {
// Paths whose persisted references can be audited without guessing 8.3 aliases.
inline bool validReferencePath(std::string_view reference) {
  if (reference.size() < 2 || reference.size() > 4096 || reference.front() != '/' || reference.back() == '/' ||
      reference.find('\0') != reference.npos || reference.find('\\') != reference.npos)
    return false;
  size_t start = 1;
  while (start < reference.size()) {
    const auto end = reference.find('/', start);
    const auto component = reference.substr(start, end == reference.npos ? reference.size() - start : end - start);
    if (component.empty() || component.size() > 765) return false;
    if (!validCheckedStorageComponent(component)) return false;
    for (unsigned char c : component)
      if (c < 0x20 || c == 0x7f) return false;
    if (end == reference.npos) break;
    start = end + 1;
  }
  for (size_t i = 0; i + 1 < reference.size(); ++i)
    if (reference[i] == '~' && reference[i + 1] >= '0' && reference[i + 1] <= '9') return false;
  return true;
}
}  // namespace syncpublish
