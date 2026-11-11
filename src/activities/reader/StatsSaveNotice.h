#pragma once
#include <atomic>
#include <cstdint>

// A retained, single-word notice. initialize() must run before activity tasks.
// On deep-sleep wake the generation survives if RTC memory remained powered.
// Acknowledging an older notice never clears a newer failure.
class StatsSaveNotice {
 public:
  explicit StatsSaveNotice(uint32_t& retainedWord) : retained(retainedWord) {}
  void initialize(bool retain) {
    std::atomic_ref<uint32_t> noticeCell(retained);
    const uint32_t value = noticeCell.load(std::memory_order_relaxed);
    if (!retain || !valid(value)) noticeCell.store(0, std::memory_order_relaxed);
  }
  void recordFailure() {
    std::atomic_ref<uint32_t> noticeCell(retained);
    uint32_t current = noticeCell.load(std::memory_order_relaxed);
    for (;;) {
      const uint32_t sequence = valid(current) ? current & SEQUENCE_MASK : 0;
      const uint32_t next = MAGIC | (sequence == SEQUENCE_MASK ? 1 : sequence + 1);
      if (noticeCell.compare_exchange_weak(current, next, std::memory_order_release, std::memory_order_relaxed)) return;
    }
  }
  uint32_t pending() const {
    std::atomic_ref<uint32_t> noticeCell(retained);
    const uint32_t value = noticeCell.load(std::memory_order_acquire);
    return valid(value) ? value : 0;
  }
  bool acknowledge(uint32_t observed) {
    if (!valid(observed)) return false;
    std::atomic_ref<uint32_t> noticeCell(retained);
    return noticeCell.compare_exchange_strong(observed, 0, std::memory_order_acq_rel);
  }

 private:
  static constexpr uint32_t MAGIC = 0xA7000000u;
  static constexpr uint32_t SEQUENCE_MASK = 0x00FFFFFFu;
  static bool valid(uint32_t value) { return (value & ~SEQUENCE_MASK) == MAGIC && (value & SEQUENCE_MASK) != 0; }
  uint32_t& retained;
};

StatsSaveNotice& statsSaveNotice();
StatsSaveNotice& settingsSaveNotice();
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
StatsSaveNotice& stateSaveNotice();
#endif
