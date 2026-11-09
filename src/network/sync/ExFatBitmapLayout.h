#pragma once

#include <cstddef>
#include <cstdint>

namespace wirecheck {
// Incremental validation of the layout assumed by SdFat's bitmap reader.
// The caller must read the complete root directory and the active FAT under
// one unchanged storage snapshot. This class performs no storage I/O.
class ExFatBitmapLayout {
 public:
  enum class State { Records, Chain, Complete, Failed };

  bool begin(uint32_t clusterCount, uint32_t clusterBytes, uint8_t numberOfFats) {
    if (started || state_ != State::Records || !clusterCount || clusterBytes < 512 ||
        (clusterBytes & (clusterBytes - 1)) || numberOfFats != 1)
      return fail();
    started = true;
    bitmapBytes = (static_cast<uint64_t>(clusterCount) + 7) / 8;
    chainLength = (bitmapBytes + clusterBytes - 1) / clusterBytes;
    if (!chainLength || chainLength > clusterCount) return fail();
    return true;
  }

  bool record(const uint8_t* entry, size_t size) {
    if (!started || state_ != State::Records || !entry || size != 32) return fail();
    if (entry[0] != 0x81) return true;
    if (found || entry[1] != 0 || readLe(entry + 20, 4) != 2 || readLe(entry + 24, 8) != bitmapBytes) return fail();
    found = true;
    return true;
  }

  // Call only after a successful root traversal to an end marker or clean end.
  bool endRecords() {
    if (!started || state_ != State::Records || !found) return fail();
    state_ = State::Chain;
    return true;
  }

  // Supply each FAT entry for clusters 2, 3, ... in order. The bitmap must
  // occupy consecutive clusters, ending with an exFAT end-of-chain marker.
  bool chainEntry(uint32_t value) {
    if (state_ != State::Chain) return fail();
    ++chainSeen;
    if (chainSeen == chainLength) {
      // Match the pinned SdFat exFAT chain reader's EXFAT_EOC value.
      if (value != 0xffffffffU) return fail();
      state_ = State::Complete;
      return true;
    }
    if (chainSeen > UINT32_MAX - 2 || value != chainSeen + 2) return fail();
    return true;
  }

  State state() const { return state_; }

 private:
  static uint64_t readLe(const uint8_t* p, size_t size) {
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i) value |= static_cast<uint64_t>(p[i]) << (8 * i);
    return value;
  }
  bool fail() {
    state_ = State::Failed;
    return false;
  }
  State state_ = State::Records;
  uint64_t bitmapBytes = 0, chainLength = 0, chainSeen = 0;
  bool started = false, found = false;
};
}  // namespace wirecheck
