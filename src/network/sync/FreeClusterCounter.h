#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace wirecheck {
// Consumes allocation-table sectors from one externally guarded snapshot.
// The caller owns geometry validation, reads, mutation checks, and cancellation.
class FreeClusterCounter {
 public:
  enum class Format { Fat16, Fat32, ExFat };
  enum class State { Idle, Reading, Complete, Failed };

  bool begin(Format volumeFormat, uint32_t clusters, uint32_t bytesPerCluster) {
    if (state_ != State::Idle ||
        (volumeFormat != Format::Fat16 && volumeFormat != Format::Fat32 && volumeFormat != Format::ExFat) ||
        !clusters || bytesPerCluster < 512 || bytesPerCluster > 32U * 1024 * 1024 ||
        (bytesPerCluster & (bytesPerCluster - 1)) != 0)
      return fail();
    format = volumeFormat;
    clusterBytes = bytesPerCluster;
    total = static_cast<uint64_t>(clusters) + (format == Format::ExFat ? 0 : 2);
    state_ = State::Reading;
    return true;
  }

  State consume(const uint8_t* sector, size_t size) {
    if (state_ != State::Reading) return state_;
    if (!sector || size != 512) {
      fail();
      return state_;
    }
    const unsigned entries = format == Format::ExFat ? 4096 : format == Format::Fat16 ? 256 : 128;
    for (unsigned i = 0; i < entries && processed < total; ++i, ++processed) {
      if (format == Format::ExFat) {
        if (!(sector[i / 8] & (1U << (i % 8)))) ++freeClusters;
      } else if (processed >= 2) {
        const unsigned width = format == Format::Fat16 ? 2 : 4;
        uint32_t value = 0;
        for (unsigned byte = 0; byte < width; ++byte)
          value |= static_cast<uint32_t>(sector[i * width + byte]) << (8 * byte);
        if (!value) ++freeClusters;
      }
    }
    if (processed == total) state_ = State::Complete;
    return state_;
  }

  State state() const { return state_; }
  bool freeBytes(uint64_t& out) const {
    if (state_ != State::Complete) return false;
    out = freeClusters * clusterBytes;
    return true;
  }

  // Reserve reader state space and two clusters for directory metadata.
  bool allowsStage(uint64_t size, uint64_t reserveBytes) const {
    uint64_t free = 0;
    if (!freeBytes(free)) return false;
    const uint64_t cluster = clusterBytes;
    if (size > std::numeric_limits<uint64_t>::max() - (cluster - 1)) return false;
    const uint64_t rounded = ((size + cluster - 1) / cluster) * cluster;
    if (reserveBytes > std::numeric_limits<uint64_t>::max() - 2 * cluster) return false;
    const uint64_t overhead = reserveBytes + 2 * cluster;
    return rounded <= free && overhead <= free - rounded;
  }

 private:
  bool fail() {
    state_ = State::Failed;
    return false;
  }
  Format format = Format::Fat16;
  State state_ = State::Idle;
  uint32_t clusterBytes = 0;
  uint64_t total = 0;
  uint64_t processed = 0;
  uint64_t freeClusters = 0;
};
}  // namespace wirecheck
