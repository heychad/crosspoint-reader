#pragma once

#include <cstddef>
#include <cstdint>

struct HalMemory {
  struct HeapStats {
    size_t largestBlockBytes;
  };
  static inline size_t largestBlock = SIZE_MAX;
  static HeapStats getDefaultHeap() { return {largestBlock}; }
};
