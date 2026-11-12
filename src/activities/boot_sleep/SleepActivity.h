#pragma once
#include <string>

#include "activities/Activity.h"

class Bitmap;
class HalFile;
class ReaderActivity;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
  void prepareReaderPageBackground(ReaderActivity* reader);
#endif

 private:
  void renderDefaultSleepScreen() const;
  void renderReadingStatsSleepScreen() const;
  void renderMinimalBookSleepScreen() const;
  void renderCustomSleepScreen() const;
  bool renderCustomSleepBmp(const std::string& path) const;
  void renderCoverSleepScreen() const;
  bool renderBitmapSleepScreen(const Bitmap& bitmap, bool preserveBackground = false) const;
  bool renderSleepOverlayFile(HalFile& file, const char* pathForLog) const;
  bool renderTransparentOverlayPng(const std::string& path) const;
  bool renderSleepOverlayPath(const std::string& path) const;
  void renderLastScreenSleepScreen() const;
  void renderTransparentCustomSleepScreen() const;
  void renderOverlayFallback() const;
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
  void renderPageOverlaySleepScreen() const;
  enum class PageOverlayBackground { RetainedScreen, PreparedPage, DisplayedPage, Unavailable };
  PageOverlayBackground pageOverlayBackground = PageOverlayBackground::RetainedScreen;
#endif
  void renderBlankSleepScreen() const;

  bool fromTimeout = false;
  // Cover + Transparent: the cover under the overlay, and the same cover while its gray planes are redrawn.
  mutable const Bitmap* overlayCover = nullptr;
  mutable const Bitmap* overlayGrayCover = nullptr;
};
