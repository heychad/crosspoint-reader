#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace epubtiming {
enum class PageKind : uint8_t { Text, Illustrated, Indexing };

struct Summary {
  uint32_t medianMs = 0;
  uint32_t maxMs = 0;
  uint8_t count = 0;
};

struct ClipSave {
  uint32_t storeMs;
  uint32_t exportMs;
};

// Accessed by the reader and diagnostics under the existing RenderLock.
// The recorder never writes to the card or allocates heap memory.
class Recorder {
 public:
  static constexpr size_t WINDOW_SIZE = 16;

  bool isRecording() const { return recording; }

  void toggleRecording() {
    recording = !recording;
    if (recording) {
      windows = {};
      clipSave.reset();
    }
  }

  void recordClipSave(ClipSave sample) {
    if (recording) clipSave = sample;
  }

  std::optional<ClipSave> lastClipSave() const { return clipSave; }

  void record(PageKind kind, uint32_t elapsedMs) {
    if (!recording) return;
    auto& window = windows[static_cast<size_t>(kind)];
    window.values[window.next] = elapsedMs;
    window.next = (window.next + 1) % WINDOW_SIZE;
    if (window.count < WINDOW_SIZE) ++window.count;
  }

  Summary summarize(PageKind kind) const {
    const auto& window = windows[static_cast<size_t>(kind)];
    if (window.count == 0) return {};
    auto sorted = window.values;
    std::sort(sorted.begin(), sorted.begin() + window.count);
    const uint32_t lower = sorted[(window.count - 1) / 2];
    const uint32_t upper = sorted[window.count / 2];
    return {lower + (upper - lower) / 2, sorted[window.count - 1], window.count};
  }

 private:
  struct Window {
    std::array<uint32_t, WINDOW_SIZE> values{};
    uint8_t next = 0;
    uint8_t count = 0;
  };
  std::array<Window, 3> windows{};
  std::optional<ClipSave> clipSave;
  bool recording = false;
};

static_assert(sizeof(Recorder) <= 220);
inline Recorder recorder;
}  // namespace epubtiming
