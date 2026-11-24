#pragma once

#include <HalMemory.h>
#include <Serialization.h>

#include <algorithm>

namespace epubcache {
inline bool readString(HalFile& file, std::string& out) {
  uint32_t length;
  if (!serialization::tryReadPod(file, length)) return false;
  if (length > out.capacity()) {
    // libstdc++ can double capacity on growth, even for a smaller request.
    const auto bytes = std::max<uint64_t>(length, uint64_t{2} * out.capacity()) + 1;
    if (bytes > HalMemory::getDefaultHeap().largestBlockBytes) return false;
  }
  return serialization::tryReadStringBytes(file, out, length);
}
}  // namespace epubcache
