#include "ReaderActivity.h"

#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <KOReaderDocumentId.h>
#include <Memory.h>
#include <TrustedTime.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "EpubReaderActivity.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "StatsSaveNotice.h"
#include "XtcReaderActivity.h"
#include "components/UITheme.h"
#include "util/PluginEvents.h"

ReaderActivity::ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               std::string bookPath, const bool allowFastInitialRefresh)
    : Activity(name, renderer, mappedInput), bookPath(std::move(bookPath)) {
  if (allowFastInitialRefresh) {
    const int refreshFrequency = SETTINGS.getRefreshFrequency();
    pagesUntilFullRefresh = refreshFrequency > 1 ? refreshFrequency : 2;
  }
}

std::unique_ptr<ReaderActivity> ReaderActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       std::string path, const bool allowFastInitialRefresh) {
  // ActivityManager requires heap ownership; each branch allocates exactly one screen-lifetime object.
  std::unique_ptr<ReaderActivity> activity;
  if (FsHelpers::hasXtcExtension(path)) {
    activity = makeUniqueNoThrow<XtcReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  } else {
    activity = makeUniqueNoThrow<EpubReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh);
  }

  if (!activity) {
    LOG_ERR("READER", "OOM: reader activity");
  }
  return activity;
}

void ReaderActivity::applyInitialOrientation() { ReaderUtils::applyOrientation(renderer, SETTINGS.orientation); }

void ReaderActivity::disableFastInitialRefresh() { pagesUntilFullRefresh = 0; }

void ReaderActivity::notePageTurn(const bool forward, const bool succeeded) {
  RenderLock lock(*this);
  readerSession.noteTurn(forward, succeeded);
  readingStats.noteTurn(forward, succeeded);
}

void ReaderActivity::onEnter() {
  Activity::onEnter();

  // Heap ledger for field crash reports: free vs largest block distinguishes a
  // leak (free falls) from fragmentation (free stable, largest collapses).
  LOG_INF("MEM", "reader enter: free=%u max_block=%u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  if (!Storage.exists(bookPath.c_str())) {
    LOG_ERR("READER", "File does not exist: %s", bookPath.c_str());
    finish();
    return;
  }

  // Clear remembered book after opening it
  if (!APP_STATE.openEpubPath.empty()) {
    APP_STATE.openEpubPath.clear();
    APP_STATE.saveToFile();
  }

  prepareReaderSettings();
  sdFontSystem.ensureLoaded(renderer);
  applyInitialOrientation();

  if (!loadBook()) {
    if (!handleLoadFailure()) finish();
    return;
  }

  {
    RenderLock lock(*this);
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
    constexpr bool completionEnabled = true;
#else
    constexpr bool completionEnabled = false;
#endif
    readingStats.begin(getBookCachePath(), bookPath, SETTINGS.trackReadingStats != 0,
                       static_cast<uint16_t>(SETTINGS.readingIdleTimeThresholdUnits) * 10U, completionEnabled);
    pendingStatsLoadWarning.store(readingStats.unavailableAtStart());
    pendingStatsSaveWarning.store(statsSaveNotice().pending() != 0);
  }
  requestUpdate();
}

void ReaderActivity::rememberBookOnceRendered() {
  if (bookRemembered || !pageRendered.load(std::memory_order_acquire)) return;
  bookRemembered = true;
  APP_STATE.openEpubPath = bookPath;
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(bookPath, getBookTitle(), getBookAuthor(), getBookThumbBmpPath());
  const pluginevents::Var openVars[] = {{"book", bookPath.c_str()}};
  pluginevents::emit(pluginevents::Event::ReaderOpen, openVars, 1);
}

void ReaderActivity::onExit() {
  finishReaderGestures();
  Activity::onExit();

  // Keep rebuildable font buffers from pinning the heap between reading sessions.
  if (auto* fontCache = renderer.getFontCacheManager()) {
    fontCache->releaseSdFontCaches();
  }

  LOG_INF("MEM", "reader exit: free=%u max_block=%u", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());

  // Flush BEFORE the ReaderExit event: the session's final progress must be
  // durable before a subscriber can act on the exit notification.
  flushReaderSession();
  if (!readingStats.finish()) statsSaveNotice().recordFailure();

  if (pluginevents::anySubscriber(pluginevents::Event::ReaderExit)) {
    const ScreenshotInfo info = getScreenshotInfo();
    char percent[8];
    char endPercent[8];
    snprintf(percent, sizeof(percent), "%d", info.progressPercent);
    snprintf(endPercent, sizeof(endPercent), "%d", info.endProgressPercent);
    const pluginevents::Var vars[] = {{"book", bookPath.c_str()}, {"percent", percent}, {"end_percent", endPercent}};
    pluginevents::emit(pluginevents::Event::ReaderExit, vars, 3);
  }

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  APP_STATE.readerActivityLoadCount = 0;
  APP_STATE.saveToFile();

  endOfBookOptions.reset();
  endOfBookOptionsReady.store(false, std::memory_order_release);
}

void ReaderActivity::prepareForSleep() {
  finishReaderGestures();
  flushReaderSession();
  if (!readingStats.finish()) statsSaveNotice().recordFailure();
}

void ReaderActivity::flushReaderSession() {
  if (!readerSession.isEmitWorthy() || !pluginevents::anySubscriber(pluginevents::Event::ReaderSession)) {
    readerSession.reset();
    return;
  }

  const std::string document = KOReaderDocumentId::calculate(bookPath);
  const bool validDocument =
      document.size() == 32 && std::all_of(document.begin(), document.end(), [](const unsigned char c) {
        return std::isdigit(c) || (c >= 'a' && c <= 'f');
      });
  if (validDocument) {
    char startTime[24];
    char endTime[24];
    char duration[16];
    char startProgress[8];
    char endProgress[8];
    snprintf(startTime, sizeof(startTime), "%lld", static_cast<long long>(readerSession.startTime()));
    snprintf(endTime, sizeof(endTime), "%lld", static_cast<long long>(readerSession.endTime()));
    snprintf(duration, sizeof(duration), "%lu", static_cast<unsigned long>(readerSession.durationSeconds()));
    snprintf(startProgress, sizeof(startProgress), "%u", readerSession.startProgressBp());
    snprintf(endProgress, sizeof(endProgress), "%u", readerSession.endProgressBp());
    const pluginevents::Var vars[] = {{"book", bookPath.c_str()},       {"document", document.c_str()},
                                      {"start_time", startTime},        {"end_time", endTime},
                                      {"duration_seconds", duration},   {"start_progress_bp", startProgress},
                                      {"end_progress_bp", endProgress}, {"progress_scale", "10000"}};
    pluginevents::emit(pluginevents::Event::ReaderSession, vars, 8);
  }
  readerSession.reset();
}

bool ReaderActivity::handleBackNavigation() {
  return ReaderUtils::handleBackNavigation(mappedInput, activityManager, bookPath.c_str(),
                                           {this, [](void* ctx) { static_cast<ReaderActivity*>(ctx)->onGoHome(); }});
}

void ReaderActivity::clearEndOfBookOptionsIfNeeded() {
  if (isAtEndOfBook() || !endOfBookOptionsReady.load(std::memory_order_acquire)) return;

  RenderLock lock(*this);
  endOfBookOptionsReady.store(false, std::memory_order_release);
  endOfBookOptions.reset();
}

bool ReaderActivity::endOfBookMenuActive() const {
  return isAtEndOfBook() && endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions->menuActive();
}

bool ReaderActivity::handleEndOfBookMenu(const bool suppressConfirmRelease) {
  if (suppressConfirmRelease || !endOfBookMenuActive()) {
    return false;
  }

  std::string openPath;
  switch (endOfBookOptions->handleMenuInput(mappedInput, &openPath)) {
    case EndOfBookOptions::Action::OpenBook:
      activityManager.goToReader(openPath);
      return true;
    case EndOfBookOptions::Action::GoHome:
      onGoHome();
      return true;
    case EndOfBookOptions::Action::LastPage:
      // A delayed Home tap belongs to the menu, not the restored page.
      mappedInput.resetHomeButtonInput();
      onReturnFromEndOfBook();
      requestUpdate();
      return true;
    case EndOfBookOptions::Action::Redraw:
      requestUpdate();
      return true;
    case EndOfBookOptions::Action::None:
      return false;
  }

  return false;
}

bool ReaderActivity::handleEndOfBookPageTurn(const bool prevTriggered, const bool nextTriggered) {
  if (!isAtEndOfBook()) return false;

  if (endOfBookOptionsReady.load(std::memory_order_acquire) && endOfBookOptions->menuActive()) {
    return true;
  }
  if (nextTriggered) {
    onGoHome();
  } else if (prevTriggered) {
    onReturnFromEndOfBook();
    requestUpdate();
  }
  return true;
}

void ReaderActivity::pollSettingsSaveWarning() {
  if (!settingsNoticeRenderReady.load(std::memory_order_acquire)) return;
  const uint32_t settingsWarning = settingsSaveNotice().pending();
  if (settingsWarning && settingsWarning != shownSettingsNotice.load() && !homeButtonFeedbackVisible.load())
    requestUpdate();
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
  const uint32_t stateWarning = stateSaveNotice().pending();
  if (stateWarning && stateWarning != shownStateNotice.load() && !homeButtonFeedbackVisible.load()) requestUpdate();
#endif
}

void ReaderActivity::loop() {
  expireHomeButtonFeedback();
  pollSettingsSaveWarning();
  if (handlePendingQuickAction()) return;
  rememberBookOnceRendered();
  clearEndOfBookOptionsIfNeeded();
  if (handleEndOfBookMenu()) return;
  if (handleFormatInput()) return;
  if (!endOfBookMenuActive() && handleHomeReaderShortcut()) return;
  if (handleBackNavigation()) return;

  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) return;
  if (handleEndOfBookPageTurn(prevTriggered, nextTriggered)) return;

  const unsigned long heldMs = (touch.prev || touch.next) ? touch.heldMs : mappedInput.getHeldTime();
  const bool skip =
      !fromTilt && SETTINGS.longPressButtonBehavior == SETTINGS.CHAPTER_SKIP && heldMs >= ReaderUtils::SKIP_HOLD_MS;

  const bool changed = skip ? skipPages(prevTriggered ? -10 : 10) : pageTurn(!prevTriggered);
  // Skips do not train forward-page pace or the plugin session.
  notePageTurn(!skip && !prevTriggered, changed);
  if (changed && (touch.prev || touch.next)) haptic_feedback::touchAction(skip);
  requestUpdate();
}

bool ReaderActivity::showPendingSettingsWarning() {
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
  const uint32_t stateWarning = stateSaveNotice().pending();
  if (stateWarning && stateWarning != shownStateNotice.load()) {
    GUI.drawPopup(renderer, tr(STR_STATE_SAVE_RECOVERY));
    shownStateNotice.store(stateWarning);
    return true;
  }
#endif
  const uint32_t observed = settingsSaveNotice().pending();
  if (!observed || observed == shownSettingsNotice.load()) return false;
  GUI.drawPopup(renderer, tr(STR_SETTINGS_SAVE_RECOVERY));
  shownSettingsNotice.store(observed);
  return true;
}

bool ReaderActivity::showPendingStatsWarning() {
  if (pendingStatsSaveWarning.exchange(false)) {
    GUI.drawPopup(renderer, tr(STR_READER_STATS_SAVE_FAILED));
    return true;
  } else if (pendingStatsLoadWarning.exchange(false)) {
    GUI.drawPopup(renderer, tr(STR_READER_STATS_UNAVAILABLE));
    return true;
  }
  return false;
}

bool ReaderActivity::showPendingHomeButtonFeedback() {
  const uint8_t feedback = pendingHomeButtonFeedback.exchange(0);
  if (!feedback) return false;
  const auto message = feedback == 1   ? StrId::STR_HOME_BUTTON_READER_ON
                       : feedback == 2 ? StrId::STR_HOME_BUTTON_READER_OFF
                                       : StrId::STR_HOME_BUTTON_SAVE_FAILED;
  GUI.drawPopup(renderer, I18N.get(message));
  return true;
}

void ReaderActivity::showNextReaderNotice(bool formatPopupDrawn) {
  if (homeButtonFeedbackVisible.load()) return;
  const bool shown = formatPopupDrawn || showPendingSettingsWarning() || showPendingStatsWarning() ||
                     showPendingFormatWarning() || showPendingHomeButtonFeedback();
  if (shown) {
    homeButtonFeedbackStarted.store(millis());
    homeButtonFeedbackVisible.store(true);
  }
}

void ReaderActivity::expireHomeButtonFeedback() {
  if (homeButtonFeedbackVisible.load() && millis() - homeButtonFeedbackStarted.load() >= 1200) {
    homeButtonFeedbackVisible.store(false);
    requestUpdate();
  }
}

void ReaderActivity::render(RenderLock&&) {
  if (isAtEndOfBook()) {
    if (!endOfBookOptions) {
      endOfBookOptions = makeUniqueNoThrow<EndOfBookOptions>(renderer);
      if (!endOfBookOptions) LOG_ERR("READER", "OOM: EndOfBookOptions");
    }
    renderer.clearScreen();
    if (endOfBookOptions) {
      endOfBookOptions->loadOnce(bookPath);
      // Release-publish AFTER loadOnce() so the main task's acquire load can't
      // observe an object whose names/selector are still being populated.
      endOfBookOptionsReady.store(true, std::memory_order_release);
      endOfBookOptions->render(renderer, mappedInput);
    }
    renderer.displayBuffer();
    onEndOfBookRendered();
    markPageRendered();
    settingsNoticeRenderReady.store(true, std::memory_order_release);
    readerSession.onRenderComplete(millis(), trustedtime::trustedNow(), getProgressBasisPoints());
    readingStats.onRender(ReadingStatsFrame::EndOfBook);
    showNextReaderNotice(false);
    return;
  }

  const uint32_t previousGeneration = renderedPageGeneration;
  const bool formatPopupDrawn = formatPopupVisible();
  renderBook();
  readerSession.onRenderComplete(millis(), trustedtime::trustedNow(), getProgressBasisPoints());
  const bool pageDrawn = previousGeneration != renderedPageGeneration;
  readingStats.onRender(pageDrawn && !inputOverPage() ? ReadingStatsFrame::Page : ReadingStatsFrame::Unavailable);
  settingsNoticeRenderReady.store(pageDrawn, std::memory_order_release);
  if (pageDrawn) {
    showNextReaderNotice(formatPopupDrawn || formatPopupVisible());
  }
}

bool ReaderActivity::handleForcedRefresh() {
  {
    RenderLock lock(*this);
    pagesUntilFullRefresh = 1;
    forcedRefreshPending = true;
  }
  requestUpdate();
  return true;
}

void ReaderActivity::onInputLockChanged(bool locked) {
  if (locked) {
    finishReaderGestures();
    onReaderGestureStarted();
    readingStats.pause();
    flushReaderSession();
  } else if (pageRendered.load(std::memory_order_acquire) && !inputOverPage()) {
    readingStats.onRender(isAtEndOfBook() ? ReadingStatsFrame::EndOfBook : ReadingStatsFrame::Page);
    readerSession.onRenderComplete(millis(), trustedtime::trustedNow(), getProgressBasisPoints());
  }
}
