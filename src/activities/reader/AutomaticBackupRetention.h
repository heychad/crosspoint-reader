#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

// Read-only selection within a snapshot group. Dates and fixed-width sequence
// numbers sort ascending; equal prefixes sort by hash. A caller must
// finish a complete scan and verify ownership before performing any deletion.
namespace statsbackup {
class AutomaticBackupRetention {
 public:
  static constexpr size_t NAME_SIZE = 58;
  static constexpr uint32_t KEEP = 7;
  explicit AutomaticBackupRetention(const char* current) {
    if (validName(current)) std::memcpy(current_.data(), current, std::strlen(current) + 1);
  }

  static char group(const char* name) {
    if (!name || std::strncmp(name, "stats_cp_", 9) != 0) return 0;
    const size_t length = std::strlen(name);
    const bool dated = length == 40 && std::memcmp(name + 17, "_A_", 3) == 0;
    const bool numbered = length == 58 && (name[20] == 'M' || name[20] == 'U') &&
                          std::memcmp(name + 21, "_00000000_000000_", 17) == 0 && name[19] == '_';
    if (!dated && !numbered) return 0;
    const size_t hashOffset = dated ? 20 : 38;
    if (std::memcmp(name + hashOffset + 16, ".bin", 4) != 0) return 0;
    for (size_t i = 9; i < (dated ? 17 : 19); ++i)
      if (name[i] < '0' || name[i] > '9') return 0;
    for (size_t i = hashOffset; i < hashOffset + 16; ++i)
      if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f'))) return 0;
    return dated ? 'A' : name[20];
  }

  static bool validName(const char* name) { return group(name) != 0; }

  static bool sequence(const char* name, uint32_t& output) {
    const char kind = group(name);
    if (kind != 'M' && kind != 'U') return false;
    uint32_t value = 0;
    for (size_t i = 9; i < 19; ++i) {
      const uint32_t digit = static_cast<uint32_t>(name[i] - '0');
      if (value > (std::numeric_limits<uint32_t>::max() - digit) / 10) return false;
      value = value * 10 + digit;
    }
    if (!value) return false;
    output = value;
    return true;
  }

  void observe(const char* name) {
    if (!validName(name) || group(name) != group(current_.data())) return;
    if (count_ != std::numeric_limits<uint32_t>::max()) ++count_;
    if (std::strcmp(name, current_.data()) == 0) {
      currentSeen_ = true;
      return;
    }
    if (!oldest_[0] || std::strcmp(name, oldest_.data()) < 0) std::memcpy(oldest_.data(), name, std::strlen(name) + 1);
  }

  // No candidate is returned until the caller certifies that enumeration
  // completed successfully and the just-verified current snapshot was seen.
  const char* candidate(bool scanComplete) const {
    return scanComplete && current_[0] && currentSeen_ && count_ > KEEP && oldest_[0] ? oldest_.data() : nullptr;
  }
  uint32_t count() const { return count_; }

 private:
  std::array<char, NAME_SIZE + 1> current_{};
  std::array<char, NAME_SIZE + 1> oldest_{};
  uint32_t count_ = 0;
  bool currentSeen_ = false;
};
}  // namespace statsbackup
