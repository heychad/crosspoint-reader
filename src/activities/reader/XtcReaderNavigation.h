#pragma once
#include <algorithm>
#include <cstdint>

// XTC progress is one-based; 100% selects the last bitmap, not the end screen.
inline uint32_t xtcPageForPercent(int percent, uint32_t pageCount) {
  if (pageCount == 0) return 0;
  const uint64_t scaled = (static_cast<uint64_t>(std::clamp(percent, 0, 100)) * pageCount + 50) / 100;
  return static_cast<uint32_t>(std::clamp<uint64_t>(scaled, 1, pageCount) - 1);
}
