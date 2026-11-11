#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "EndOfBookOptions.h"
#include "ReaderSession.h"
#include "ReaderStats.h"
#include "activities/Activity.h"
#include "personalization/QuickLockTrigger.h"
#include "personalization/ReaderGestures.h"

class ReaderActivity : public Activity {
  friend class ActivityManager;  // turns pages for a Bluetooth remote (ActivityManager::remoteTurn)

 protected:
#if FREEINK_CAP_TOUCH
  readergestures::Controller gestureController;
  readergestures::Frame gestureFrame;
  struct LightGesture {
    bool active = false;
    bool initialOn = false;
    uint8_t initialValue = 0;
    readershortcuts::Action action = readershortcuts::Action::Default;
  } lightGesture;
  void applyLightGesture(const readergestures::Event& event);
  bool applyReaderGestureResult(const readergestures::Result& result);
#endif
  virtual bool handleReaderShortcut(readershortcuts::Action action, QuickLockTrigger trigger = QuickLockTrigger::None);
  virtual bool supportsReaderShortcut(readershortcuts::Action action) const;
  void openQuickActions();
  bool handlePendingQuickAction();
  bool handleHomeReaderShortcut();
  std::atomic<readershortcuts::Action> pendingQuickAction{readershortcuts::Action::Default};
  uint32_t quickActionPaintGeneration = 0;
  // Called with the render lock held, before dispatching a new gesture.
  virtual void onReaderGestureStarted() {}
  bool handleReaderGestureInput();
  void finishReaderGestures();
  std::string bookPath;
  int pagesUntilFullRefresh = 0;
  bool forcedRefreshPending = false;

  std::unique_ptr<EndOfBookOptions> endOfBookOptions;
  std::atomic<bool> endOfBookOptionsReady{false};
  ReaderSession readerSession;
  ReaderStats readingStats;
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
  ReaderStats::CompletionResult setBookCompleted(bool completed);
  ReaderStats::CompletionResult recoverBookCompletion();
#endif
  std::atomic<bool> pendingStatsLoadWarning{false};
  std::atomic<bool> pendingStatsSaveWarning{false};
  std::atomic<uint32_t> shownSettingsNotice{0};
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
  std::atomic<uint32_t> shownStateNotice{0};
#endif
  std::atomic<bool> settingsNoticeRenderReady{false};
  // 1 = enabled, 2 = disabled, 3 = save failed. The render task owns painting.
  std::atomic<uint8_t> pendingHomeButtonFeedback{0};
  std::atomic<bool> homeButtonFeedbackVisible{false};
  std::atomic<unsigned long> homeButtonFeedbackStarted{0};
  bool showPendingHomeButtonFeedback();
  void expireHomeButtonFeedback();
  bool showPendingStatsWarning();
  bool showPendingSettingsWarning();
  void pollSettingsSaveWarning();
  virtual bool showPendingFormatWarning() { return false; }
  virtual bool formatPopupVisible() const { return false; }
  void showNextReaderNotice(bool formatPopupDrawn);
  std::atomic<bool> pageRendered{false};
  bool bookRemembered = false;
  uint32_t renderedPageGeneration = 0;
  void markPageRendered() {
    ++renderedPageGeneration;
    pageRendered.store(true, std::memory_order_release);
  }
  void rememberBookOnceRendered();

  explicit ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                          std::string bookPath, bool allowFastInitialRefresh);

  virtual void prepareReaderSettings() {}
  virtual bool loadBook() = 0;
  virtual const std::string& getBookCachePath() const = 0;
  // Called when loadBook() failed. Return true to keep the activity alive
  // (e.g. showing a dialog); false finishes it (the default).
  virtual bool handleLoadFailure() { return false; }
  virtual std::string getBookTitle() const = 0;
  virtual std::string getBookAuthor() const { return ""; }
  virtual std::string getBookThumbBmpPath() const { return ""; }
  // Whole-book progress for the reader.exit plugin event, reusing the
  // per-reader ScreenshotInfo implementations.
  //
  // Upstream added this helper in d3e55c53 with an earlier slice of this
  // feature, then removed it in c1e1fec3 as dead code once no caller remained
  // ("removes unnecessary wrapper structs"). It is reinstated here because this
  // change reintroduces the callers: ReaderActivity.cpp uses it for the progress
  // string and for the session's render-complete basis points, and
  // EpubReaderActivity overrides getProgressBasisPoints() and falls back to it.
  int getProgressPercent() const { return getScreenshotInfo().progressPercent; }
  virtual int getProgressBasisPoints() const { return getProgressPercent() * 100; }

  virtual bool handleFormatInput() { return false; }
  virtual bool pageTurn(bool isForward) = 0;
  virtual bool skipPages(int amount) { return pageTurn(amount > 0); }
  // A Bluetooth remote asks for the reader menu (true) or a new clipping (false).
  virtual bool remoteShortcut(bool /*readerMenu*/) { return false; }
  virtual bool isAtEndOfBook() const = 0;
  virtual void onReturnFromEndOfBook() {}

  virtual void renderBook() = 0;
  virtual void applyInitialOrientation();
  virtual void onEndOfBookRendered() {}

  bool handleBackNavigation();
  /** True while the end-of-book suggestion menu is on screen and owning input. */
  bool endOfBookMenuActive() const;
  /** True while a toolbar, panel or menu over the page owns the device keys: a remote turn is refused. */
  virtual bool inputOverPage() const {
    return pendingQuickAction != readershortcuts::Action::Default || endOfBookMenuActive();
  }
  bool handleEndOfBookMenu(bool suppressConfirmRelease = false);
  bool handleEndOfBookPageTurn(bool prevTriggered, bool nextTriggered);
  void clearEndOfBookOptionsIfNeeded();
  void disableFastInitialRefresh();
  void notePageTurn(bool forward, bool succeeded);
  void flushReaderSession();

 public:
  ~ReaderActivity() override = default;

  static std::unique_ptr<ReaderActivity> create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                std::string path, bool allowFastInitialRefresh);

  void onEnter() override;
  void onExit() override;
  void prepareForSleep() override;
  void onInputLockChanged(bool locked) override;
  void loop() override;
  void render(RenderLock&& lock) override;

  bool isReaderActivity() const final { return true; }
  ReaderActivity* bookReader() final { return this; }
  bool isReaderPage() const final { return !inputOverPage(); }
  bool acceptsRemoteInput() const final { return pageRendered.load(std::memory_order_acquire); }
  bool handleForcedRefresh() final;
};
