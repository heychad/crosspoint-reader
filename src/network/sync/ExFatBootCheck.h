#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wirecheck {
// Checks selected main boot geometry, flags, and checksum against the mounted
// volume. The caller supplies 12 consecutive, cache-coherent 512-byte sectors
// from one unchanged storage snapshot. No writes or dynamic allocation.
class ExFatBootCheck {
 public:
  enum class State { Idle, Reading, Complete, Failed };
  struct Expected {
    uint32_t startSector, partitionSectors, deviceSectors, fatStart, heapStart, clusterCount, clusterBytes;
  };

  bool begin(const Expected& expected) {
    if (state_ != State::Idle || !expected.partitionSectors || expected.startSector >= expected.deviceSectors ||
        uint64_t(expected.startSector) + expected.partitionSectors > expected.deviceSectors ||
        expected.partitionSectors < 12)
      return fail();
    expected_ = expected;
    state_ = State::Reading;
    return true;
  }

  bool consume(const uint8_t* sector, size_t size) {
    if (state_ != State::Reading || !sector || size != 512) return fail();
    if (nextSector_ == 0 && !checkBoot(sector)) return fail();
    if (nextSector_ < 11) {
      for (size_t i = 0; i < 512; ++i) {
        if (nextSector_ == 0 && (i == 106 || i == 107 || i == 112)) continue;
        checksum_ = (checksum_ >> 1) | (checksum_ << 31);
        checksum_ += sector[i];
      }
    } else {
      for (size_t i = 0; i < 512; i += 4)
        if (readLe(sector + i, 4) != checksum_) return fail();
      state_ = State::Complete;
    }
    ++nextSector_;
    return true;
  }
  State state() const { return state_; }

 private:
  static uint64_t readLe(const uint8_t* p, size_t size) {
    uint64_t result = 0;
    for (size_t i = 0; i < size; ++i) result |= uint64_t(p[i]) << (8 * i);
    return result;
  }
  bool checkBoot(const uint8_t* s) const {
    if (std::memcmp(s + 3, "EXFAT   ", 8) || s[510] != 0x55 || s[511] != 0xaa || s[108] != 9 || s[109] > 16 ||
        s[110] != 1 || readLe(s + 104, 2) != 0x100 || (readLe(s + 106, 2) & 7) != 0)
      return false;
    const uint64_t volumeLength = readLe(s + 72, 8), fatOffset = readLe(s + 80, 4);
    const uint64_t fatLength = readLe(s + 84, 4), heapOffset = readLe(s + 88, 4);
    const uint64_t clusters = readLe(s + 92, 4), root = readLe(s + 96, 4);
    const uint64_t clusterBytes = uint64_t(512) << s[109];
    if (!clusters || root < 2 || root > clusters + 1 || readLe(s + 64, 8) != expected_.startSector ||
        volumeLength > expected_.partitionSectors || fatOffset < 24 || !fatLength ||
        fatOffset + fatLength > heapOffset || fatLength * 512 < (clusters + 2) * 4 ||
        heapOffset + clusters * (clusterBytes / 512) > volumeLength)
      return false;
    return uint64_t(expected_.startSector) + fatOffset == expected_.fatStart &&
           uint64_t(expected_.startSector) + heapOffset == expected_.heapStart && clusters == expected_.clusterCount &&
           clusterBytes == expected_.clusterBytes;
  }
  bool fail() {
    state_ = State::Failed;
    return false;
  }
  Expected expected_{};
  uint32_t checksum_ = 0;
  uint8_t nextSector_ = 0;
  State state_ = State::Idle;
};
}  // namespace wirecheck
