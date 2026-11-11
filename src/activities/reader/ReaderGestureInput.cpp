#include <BoardConfig.h>
#include <HalFrontlight.h>
#include <HalTiltSensor.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "ReaderActivity.h"
#include "ReadingStatsStore.h"
#include "activities/settings/ReadingStatsActivity.h"
#include "util/ScreenshotUtil.h"

using readershortcuts::Action;

bool ReaderActivity::handleReaderShortcut(Action action, QuickLockTrigger trigger) {
  switch (action) {
    case Action::SleepOnly:
    case Action::WakeOnly:
    case Action::TogglePageGestures:
    case Action::JoinNetwork:
    case Action::CreateHotspot:
    case Action::CalibreWireless:
      return activityManager.dispatchShortcut(action, trigger);
    case Action::Default:
      return false;
    case Action::Refresh:
      return handleForcedRefresh();
    case Action::PreviousPage:
    case Action::NextPage: {
      const bool forward = action == Action::NextPage;
      if (handleEndOfBookPageTurn(!forward, forward)) return true;
      bool changed = false;
      {
        RenderLock lock(*this);
        changed = pageTurn(forward);
      }
      notePageTurn(forward, changed);
      if (changed) requestUpdate();
      return true;
    }
    case Action::Screenshot: {
      RenderLock lock(*this);
      ScreenshotUtil::takeScreenshot(renderer);
      return true;
    }
    case Action::QuickLock:
      activityManager.requestInputLock(trigger);
      return true;
    case Action::Sleep:
      activityManager.requestSleep();
      return true;
    case Action::QuickActions:
      openQuickActions();
      return true;
    case Action::Home:
      onGoHome();
      return true;
    case Action::HomeReader:
      return activityManager.handleHomeReaderShortcut();
    case Action::FileBrowser:
      activityManager.goToFileBrowser();
      return true;
    case Action::Library:
      activityManager.goToLibrary();
      return true;
    case Action::FileTransfer:
      activityManager.goToFileTransfer();
      requestUpdate();
      return true;
    case Action::SavedItems:
      activityManager.goToSavedItems();
      requestUpdate();
      return true;
    case Action::ReadingStatistics: {
      std::unique_ptr<ReadingStatsActivity> screen;
      {
        RenderLock lock(*this);
        if (pageRendered.load(std::memory_order_acquire)) {
          bool tracking = true;
          bool saved = true;
          BookReadingStats book;
          GlobalReadingStats global;
          const bool liveStats = readingStats.snapshot(book, global, saved);
          if (!liveStats) {
            book = readingstats::loadBook(getBookCachePath(), bookPath, tracking);
            global = GlobalReadingStats::load();
          }
          screen = makeUniqueNoThrow<ReadingStatsActivity>(
              renderer, mappedInput, getBookTitle(), book, global, getProgressBasisPoints(), tracking, saved,
              getBookCachePath(), bookPath, liveStats ? &readingStats : nullptr);
        } else {
          readingStats.pause();
          screen = makeUniqueNoThrow<ReadingStatsActivity>(renderer, mappedInput);
        }
      }
      if (!screen) {
        LOG_ERR("READER", "OOM: reading statistics");
        requestUpdate();
        return true;
      }
      startActivityForResult(std::move(screen), [this](const ActivityResult&) { requestUpdate(); });
      return true;
    }
    case Action::ToggleNight: {
      {
        RenderLock lock(*this);
        SETTINGS.screenInverted = SETTINGS.screenInverted ? 0 : 1;
      }
      SETTINGS.saveToFile();
      requestUpdate();
      return true;
    }
    case Action::ToggleTouch: {
      {
        RenderLock lock(*this);
        SETTINGS.touchReaderControls = SETTINGS.touchReaderControls ? 0 : 1;
      }
      SETTINGS.saveToFile();
      requestUpdate();
      return true;
    }
    case Action::ToggleHomeButton: {
      if (!BoardConfig::hasHomeKey()) return true;
      const uint8_t previous = SETTINGS.homeButtonInReaderEnabled;
      SETTINGS.homeButtonInReaderEnabled = previous ? 0 : 1;
      if (!SETTINGS.saveToFile()) {
        SETTINGS.homeButtonInReaderEnabled = previous;
        LOG_ERR("READER", "Failed to save Home button setting");
        pendingHomeButtonFeedback.store(3);
      } else {
        pendingHomeButtonFeedback.store(SETTINGS.homeButtonInReaderEnabled ? 1 : 2);
      }
      mappedInput.resetHomeButtonInput();
      requestUpdate();
      return true;
    }
    case Action::ToggleTilt:
      if (halTiltSensor.isAvailable()) {
        {
          RenderLock lock(*this);
          SETTINGS.tiltPageTurn =
              SETTINGS.tiltPageTurn ? CrossPointSettings::TILT_OFF : CrossPointSettings::TILT_NORMAL;
        }
        SETTINGS.saveToFile();
        requestUpdate();
      }
      return true;
    default:
      break;  // Light controls below; format-specific actions belong to the derived reader.
  }
  if (!Frontlight.present()) return true;
  switch (action) {
    case Action::ToggleFrontlight:
      Frontlight.setOn(!Frontlight.isOn());
      SETTINGS.frontlightOn = Frontlight.isOn();
      break;
    case Action::Brighter:
    case Action::Dimmer:
      Frontlight.setBrightness(
          std::clamp<int>(Frontlight.brightness() + (action == Action::Brighter ? 10 : -10), 0, 100));
      if (action == Action::Brighter) Frontlight.setOn(true);
      SETTINGS.frontlightBrightness = Frontlight.brightness();
      SETTINGS.frontlightOn = Frontlight.isOn();
      break;
    case Action::Warmer:
    case Action::Cooler:
      if (!Frontlight.hasColorTemperature()) return true;
      Frontlight.setWarmth(std::clamp<int>(Frontlight.warmth() + (action == Action::Warmer ? 10 : -10), 0, 100));
      SETTINGS.frontlightWarmth = Frontlight.warmth();
      break;
    // EPUB owns navigation and typography; pre-rendered formats consume unsupported bindings.
    default:
      return true;
  }
  SETTINGS.saveToFile();
  return true;
}

#if FREEINK_CAP_TOUCH
void ReaderActivity::applyLightGesture(const readergestures::Event& event) {
  using readergestures::EventKind;
  if (event.kind == EventKind::LightBegin) {
    if (!Frontlight.present()) return;
    const bool warmth = event.action == Action::Warmer || event.action == Action::Cooler;
    if (warmth && !Frontlight.hasColorTemperature()) return;
    lightGesture = {true, Frontlight.isOn(), warmth ? Frontlight.warmth() : Frontlight.brightness(), event.action};
  }
  if (!lightGesture.active) return;
  const bool warmth = lightGesture.action == Action::Warmer || lightGesture.action == Action::Cooler;
  if (event.kind == EventKind::LightBegin || event.kind == EventKind::LightUpdate ||
      event.kind == EventKind::LightCancel) {
    const bool increase = lightGesture.action == Action::Brighter || lightGesture.action == Action::Warmer;
    const int amount = event.kind == EventKind::LightCancel ? 0 : event.amount;
    const uint8_t value = std::clamp<int>(lightGesture.initialValue + (increase ? amount : -amount), 0, 100);
    if (warmth) {
      Frontlight.setWarmth(value);
      SETTINGS.frontlightWarmth = value;
    } else {
      Frontlight.setBrightness(value);
      Frontlight.setOn(increase && amount > 0 ? true : lightGesture.initialOn);
      SETTINGS.frontlightBrightness = value;
      SETTINGS.frontlightOn = Frontlight.isOn();
    }
  }
  if (event.kind == EventKind::LightFinish || event.kind == EventKind::LightCancel) {
    const uint8_t value = warmth ? Frontlight.warmth() : Frontlight.brightness();
    const bool changed = value != lightGesture.initialValue || Frontlight.isOn() != lightGesture.initialOn;
    lightGesture.active = false;
    if (changed) SETTINGS.saveToFile();
  }
}
#endif

#if FREEINK_CAP_TOUCH
bool ReaderActivity::applyReaderGestureResult(const readergestures::Result& result) {
  if (result.suppressContact) mappedInput.suppressTouchContact();
  for (size_t i = 0; i < result.count; ++i) {
    const auto& event = result.events[i];
    if (event.kind == readergestures::EventKind::Discrete)
      handleReaderShortcut(event.action);
    else
      applyLightGesture(event);
  }
  return result.consumed;
}
#endif

bool ReaderActivity::handleReaderGestureInput() {
#if FREEINK_CAP_TOUCH
  if (!mappedInput.hasTouch()) return false;
  if (!SETTINGS.touchReaderControls) {
    return applyReaderGestureResult(gestureController.suspend(mappedInput.touchContactsDown(), true));
  }
  readergestures::Result result;
  {
    RenderLock lock(RenderLock::Mode::Try);
    if (!lock.ownsLock()) {
      return applyReaderGestureResult(gestureController.suspend(mappedInput.touchContactsDown(), false));
    }
    auto& frame = gestureFrame;
    frame = {};
    {
      const auto snapshot = mappedInput.getTouchSnapshot();
      frame.count = snapshot.count;
      frame.reportedCount = snapshot.reportedCount;
      frame.idsStable = snapshot.idsStable;
      for (size_t i = 0; i < frame.points.size() && i < snapshot.count; ++i) {
        const auto& point = snapshot.contacts[i];
        frame.points[i] = {point.x, point.y, point.id, point.valid};
      }
    }
    frame.multiSupported = mappedInput.supportsMultiTouch();
    frame.now = millis();
    frame.width = renderer.getScreenWidth();
    frame.height = renderer.getScreenHeight();
    frame.orientation = static_cast<uint8_t>(renderer.getOrientation());
    readergestures::Config config;
    config.enabled = pageRendered.load(std::memory_order_acquire) && !inputOverPage() && !isAtEndOfBook();
    config.pinch = SETTINGS.pinchFontResizeEnabled;
    config.rotation = SETTINGS.twoFingerRotationEnabled;
    config.multi = SETTINGS.twoFingerActions;
    config.edges = {static_cast<Action>(SETTINGS.readerLeftEdgeUp), static_cast<Action>(SETTINGS.readerLeftEdgeDown),
                    static_cast<Action>(SETTINGS.readerRightEdgeUp), static_cast<Action>(SETTINGS.readerRightEdgeDown)};
    {
      MappedInputManager::CompletedSwipe swipe;
      frame.swipe = mappedInput.wasCompletedMultiTouchSwipe(swipe);
      if (frame.swipe)
        frame.completedSwipe = {swipe.contactCount, swipe.startX, swipe.startY,
                                swipe.endX,         swipe.endY,   static_cast<uint32_t>(swipe.durationMs)};
      MappedInputManager::CompletedTransform transform;
      frame.rotation = mappedInput.wasCompletedMultiTouchRotation(transform);
      frame.rotationDegrees = transform.value;
      frame.pinch = mappedInput.wasCompletedMultiTouchPinch(transform);
      frame.pinchScale = transform.value;
      int sx = 0, sy = 0;
      frame.singleSwipe = mappedInput.completedSingleSwipe(sx, sy, frame.endX, frame.endY);
    }
    result = gestureController.update(frame, config);
    if (result.started) onReaderGestureStarted();
  }
  return applyReaderGestureResult(result);
#else
  return false;
#endif
}

void ReaderActivity::finishReaderGestures() {
#if FREEINK_CAP_TOUCH
  applyReaderGestureResult(gestureController.cancel(mappedInput.touchContactsDown()));
#endif
}
