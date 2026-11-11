#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

// Read-only selection for automatic snapshots. Dates sort chronologically;
// snapshots on the same date sort by hash, not creation time. A caller must
// finish a complete scan and verify ownership before performing any deletion.
namespace statsbackup {
class AutomaticBackupRetention {
 public:
  static constexpr size_t NAME_SIZE = 40;
  static constexpr uint32_t KEEP = 7;
  explicit AutomaticBackupRetention(const char* current) {
    if (validName(current)) std::memcpy(current_.data(), current, NAME_SIZE + 1);
  }

  static bool validName(const char* name) {
    if (!name || std::strlen(name) != NAME_SIZE || std::strncmp(name, "stats_cp_", 9) != 0 ||
        std::memcmp(name + 17, "_A_", 3) != 0 || std::memcmp(name + 36, ".bin", 4) != 0)
      return false;
    for (size_t i = 9; i < 17; ++i)
      if (name[i] < '0' || name[i] > '9') return false;
    for (size_t i = 20; i < 36; ++i)
      if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f'))) return false;
    return true;
  }

  void observe(const char* name) {
    if (!validName(name)) return;
    if (count_ != std::numeric_limits<uint32_t>::max()) ++count_;
    if (std::strcmp(name, current_.data()) == 0) {
      currentSeen_ = true;
      return;
    }
    if (!oldest_[0] || std::strcmp(name, oldest_.data()) < 0) std::memcpy(oldest_.data(), name, NAME_SIZE + 1);
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
