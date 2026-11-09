#pragma once
#include <cstddef>
#include <cstdint>

namespace readerfonts {
// Works with sparse, unordered installed sizes without allocating a temporary list.
template <typename SizeAt>
bool adjacentSize(size_t count, SizeAt sizeAt, uint8_t current, bool larger, uint8_t& result) {
  uint8_t nearest = 0;
  unsigned best = 256;
  for (size_t i = 0; i < count; ++i) {
    const uint8_t size = sizeAt(i);
    if (!size) continue;
    const unsigned distance = size > current ? size - current : current - size;
    if (distance < best || (distance == best && size < nearest)) {
      nearest = size;
      best = distance;
    }
  }
  if (!nearest) return false;
  uint8_t next = 0;
  for (size_t i = 0; i < count; ++i) {
    const uint8_t size = sizeAt(i);
    if (larger ? size > nearest : (size != 0 && size < nearest)) {
      if (!next || (larger ? size < next : size > next)) next = size;
    }
  }
  if (!next || next == current) return false;
  result = next;
  return true;
}
}  // namespace readerfonts
