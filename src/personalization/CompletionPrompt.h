#pragma once

#include <cmath>
#include <cstdint>

// Reader-owned state, accessed under its render lock. A prompt requires a
// below-to-above crossing followed by leaving the threshold's full chapter.
class CompletionPrompt {
 public:
  void configure(int spine, float progress) {
    thresholdSpine = spine;
    thresholdProgress = progress;
    phase =
        spine >= 0 && std::isfinite(progress) && progress >= 0 && progress <= 1 ? Phase::AwaitBelow : Phase::Disabled;
  }
  void observe(int spine, float progress, bool completed) {
    if (completed) {
      phase = Phase::Shown;
      return;
    }
    if (!std::isfinite(progress)) return;
    const bool above = spine > thresholdSpine || (spine == thresholdSpine && progress >= thresholdProgress);
    if (phase == Phase::AwaitBelow && !above) phase = Phase::AwaitCrossing;
    if (phase == Phase::AwaitCrossing && above) phase = Phase::AwaitExit;
  }
  bool queueOnChapterExit(int spine, bool completeChapter) {
    if (phase != Phase::AwaitExit || spine != thresholdSpine || !completeChapter) return false;
    phase = Phase::Queued;
    return true;
  }
  bool queued() const { return phase == Phase::Queued; }
  void shown() { phase = Phase::Shown; }
  void retryOnNextExit() { phase = Phase::AwaitExit; }

 private:
  enum class Phase : uint8_t { Disabled, AwaitBelow, AwaitCrossing, AwaitExit, Queued, Shown };
  int thresholdSpine = -1;
  float thresholdProgress = 1;
  Phase phase = Phase::Disabled;
};

static_assert(sizeof(CompletionPrompt) <= 16);
