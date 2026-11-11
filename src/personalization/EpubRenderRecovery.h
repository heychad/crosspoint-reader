#pragma once

#include <Epub/EpubBuildFailure.h>
#include <Epub/ReaderRenderSpec.h>

#include <optional>

// Owned by the EPUB reader and accessed under its render lock.
class EpubRenderRecovery {
  enum class Profile : uint8_t { Balanced, Light, Safe };

 public:
  ReaderRenderSpec spec(ReaderRenderSpec base, int spine) const {
    if (successful) apply(base, *successful);
    if (trial && spine == trialSpine) apply(base, *trial);
    return base;
  }
  bool retry(EpubBuildFailure failure, ReaderRenderSpec base, int spine) {
    if (failure != EpubBuildFailure::OutOfMemory) return false;
    const auto current = spec(base, spine);
    Profile next;
    switch (current.renderMode) {
      case EpubRenderMode::Default:
        next = Profile::Balanced;
        break;
      case EpubRenderMode::Balanced:
        next = Profile::Light;
        break;
      case EpubRenderMode::Light:
        if (!current.embeddedStyle && !current.focusReadingEnabled) return false;
        next = Profile::Safe;
        break;
      default:
        return false;
    }
    trialSpine = spine;
    trial = next;
    return true;
  }
  bool pageRendered(int spine) {
    if (!trial || spine != trialSpine) return false;
    successful = trial;
    trial.reset();
    return true;
  }
  bool safeActive() const { return successful == Profile::Safe; }
  void cancelTrial() {
    trial.reset();
    trialSpine = -1;
  }
  void reset() {
    trial.reset();
    successful.reset();
    trialSpine = -1;
  }

 private:
  static void apply(ReaderRenderSpec& spec, Profile profile) {
    const auto mode = profile == Profile::Balanced ? EpubRenderMode::Balanced : EpubRenderMode::Light;
    if (static_cast<uint8_t>(mode) > static_cast<uint8_t>(spec.renderMode)) spec.renderMode = mode;
    if (profile == Profile::Safe) {
      spec.embeddedStyle = false;
      spec.focusReadingEnabled = false;
    }
  }
  std::optional<Profile> trial;
  std::optional<Profile> successful;
  int trialSpine = -1;
};
