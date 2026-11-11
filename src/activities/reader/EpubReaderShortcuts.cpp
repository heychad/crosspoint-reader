#include <GfxRenderer.h>
#include <HalFrontlight.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "EpubReaderActivity.h"
#include "MappedInputManager.h"
#include "ReaderFontSizes.h"
#include "ReaderUtils.h"
#include "SdCardFontSystem.h"
#include "util/ReadFolder.h"

using readershortcuts::Action;
using readershortcuts::Trigger;

bool EpubReaderActivity::customSideShortcuts() const {
  return SETTINGS.readerUpTap || SETTINGS.readerUpHold || SETTINGS.readerDownTap || SETTINGS.readerDownHold ||
         SETTINGS.readerSideChord > 1;
}

bool EpubReaderActivity::handleReaderShortcut(Action action, QuickLockTrigger trigger) {
  if (action == Action::Default) return false;
  automaticPageTurnActive = false;
  switch (action) {
    case Action::Default:
      return false;
    case Action::Disabled:
      return true;
    case Action::PreviousPage:
    case Action::NextPage:
      if (handleEndOfBookPageTurn(action == Action::PreviousPage, action == Action::NextPage)) return true;
      if (section) {
        pendingManualTurn = action == Action::NextPage ? 1 : -1;
        pendingManualTurnTouch = false;
        requestUpdate();
      }
      return true;
    case Action::PreviousChapter:
    case Action::NextChapter:
      if (section) {
        const bool moved = skipPages(action == Action::NextChapter ? 1 : -1);
        notePageTurn(false, moved);
        requestUpdate();
      }
      return true;
    case Action::SelectChapter:
      openChapterSelection(false);
      return true;
    case Action::Menu:
      if (usesToolbarMenu() && section)
        openOverlay(Overlay::Toolbar);
      else
        openReaderMenu();
      return true;
    case Action::Bookmark:
      if (section) {
        addBookmark();
        showBookmarkMessage = true;
        bookmarkMessageTime = millis();
        requestUpdate();
      }
      return true;
    case Action::Dictionary:
      if (section) openDictionaryWordSelect();
      return true;
    case Action::Footnotes:
      if (footnoteDepth > 0)
        restoreSavedPosition();
      else if (section)
        openFootnoteSelect(false);
      return true;
    case Action::Refresh:
      handleForcedRefresh();
      return true;
    case Action::ToggleFrontlight:
    case Action::Brighter:
    case Action::Dimmer:
    case Action::Warmer:
    case Action::Cooler:
    case Action::QuickActions:
    case Action::QuickLock:
    case Action::Sleep:
    case Action::SleepOnly:
    case Action::WakeOnly:
    case Action::Home:
    case Action::HomeReader:
    case Action::FileBrowser:
    case Action::FileTransfer:
    case Action::Library:
    case Action::SavedItems:
    case Action::ReadingStatistics:
    case Action::ToggleNight:
    case Action::ToggleTouch:
    case Action::ToggleTilt:
    case Action::TogglePageGestures:
    case Action::JoinNetwork:
    case Action::CreateHotspot:
    case Action::CalibreWireless:
    case Action::ToggleHomeButton:
      return ReaderActivity::handleReaderShortcut(action, trigger);
    case Action::Screenshot: {
      {
        RenderLock lock(*this);
        pendingScreenshot = true;
      }
      requestUpdate();
      return true;
    }
    case Action::SyncProgress:
      launchKOReaderSync();
      return true;
    case Action::CreateClipping:
      if (section) startClipSelection();
      return true;
    case Action::FontFamily:
      openFontFamilyPicker(false);
      return true;
    case Action::CycleFontFamily: {
      cycleFontFamily();
      return true;
    }
    case Action::ToggleCompletion: {
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION) && defined(CROSSPOINT_EXPERIMENTAL_COMPLETION_PROMPT)
      bool completed = false;
      bool known = false;
      {
        RenderLock lock(*this);
        known = readingStats.completionStatus(completed);
      }
      if (known && (completed || !SETTINGS.moveFinishedToReadFolder || isInReadFolder(bookPath)))
        handleCompletionAttempt(false, !completed);
#endif
      return true;
    }
    case Action::ToggleFocus: {
      {
        RenderLock lock(*this);
        readingStats.pause();
        SETTINGS.focusReadingEnabled = SETTINGS.focusReadingEnabled ? 0 : 1;
      }
      applyReaderTextSettings();
      requestUpdate();
      return true;
    }
    case Action::CycleAutoTurn:
      cycleAutoPageTurn();
      return true;
    case Action::LargerText:
    case Action::SmallerText: {
      {
        RenderLock lock(*this);
        uint8_t next = SETTINGS.fontPointSize;
        if (!adjacentReaderFontPointSize(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName, SETTINGS.fontPointSize,
                                         action == Action::LargerText, next))
          return true;
        readingStats.pause();
        SETTINGS.fontPointSize = next;
        pendingManualTurn = 0;
        pendingManualTurnTouch = false;
      }
      applyReaderTextSettings();
      requestUpdate();
      return true;
    }
    case Action::Rotate:
    case Action::RotateBackward:
    case Action::RotateHalfTurn:
      applyOrientation(
          (SETTINGS.orientation + (action == Action::Rotate           ? 1
                                   : action == Action::RotateHalfTurn ? 2
                                                                      : CrossPointSettings::ORIENTATION_COUNT - 1)) %
          CrossPointSettings::ORIENTATION_COUNT);
      requestUpdate();
      return true;
    case Action::Count:
      return true;
  }
  return true;
}

bool EpubReaderActivity::handleReaderShortcutInput() {
  using Button = MappedInputManager::Button;
  static_assert(CrossPointSettings::OFF == 0 && CrossPointSettings::CHAPTER_SKIP == 1 &&
                CrossPointSettings::ORIENTATION_CHANGE == 2);
  static_assert(CrossPointSettings::PORTRAIT == 0);
  const bool swapped = readershortcuts::swapSideBindings(SETTINGS.readerSideFollowOrientation, SETTINGS.orientation);
  if (swapped != shortcutSideSwapped) {
    shortcutTracker.reset();
    shortcutSideSwapped = swapped;
  }
  const auto defaultAction = [this](bool up, bool hold) {
    return readershortcuts::defaultAction(up, hold, SETTINGS.sideButtonLayout, mappedInput.isNavDirectionSwapped(),
                                          SETTINGS.longPressButtonBehavior);
  };
  const auto holdAction = [&](bool up) {
    const bool logicalUp = readershortcuts::logicalSideUp(up, swapped);
    const auto action = static_cast<Action>(logicalUp ? SETTINGS.readerUpHold : SETTINGS.readerDownHold);
    return action == Action::Default ? defaultAction(up, true) : action;
  };
  const std::array<readershortcuts::Contact, 2> contacts{
      {{mappedInput.isPressed(Button::Up), mappedInput.wasPressed(Button::Up), mappedInput.wasReleased(Button::Up)},
       {mappedInput.isPressed(Button::Down), mappedInput.wasPressed(Button::Down),
        mappedInput.wasReleased(Button::Down)}}};
  // The existing Power+side screenshot shortcut keeps ownership of its contact.
  const bool powerDown = mappedInput.isPressed(Button::Power);
  const auto event = shortcutTracker.update(
      millis(), contacts, customSideShortcuts() && !powerDown,
      {holdAction(true) != Action::Disabled, holdAction(false) != Action::Disabled}, SETTINGS.readerSideChord > 1);
  if (event.trigger != Trigger::None) {
    if (event.trigger == Trigger::UpHold || event.trigger == Trigger::Chord)
      mappedInput.consumeSideButtonUntilReleased(Button::Up);
    if (event.trigger == Trigger::DownHold || event.trigger == Trigger::Chord)
      mappedInput.consumeSideButtonUntilReleased(Button::Down);
    Action action = Action::Disabled;
    switch (event.trigger) {
      case Trigger::UpTap:
        action = static_cast<Action>(swapped ? SETTINGS.readerDownTap : SETTINGS.readerUpTap);
        break;
      case Trigger::DownTap:
        action = static_cast<Action>(swapped ? SETTINGS.readerUpTap : SETTINGS.readerDownTap);
        break;
      case Trigger::UpHold:
        action = holdAction(true);
        break;
      case Trigger::DownHold:
        action = holdAction(false);
        break;
      case Trigger::Chord:
        action = static_cast<Action>(SETTINGS.readerSideChord);
        break;
      case Trigger::None:
        break;
    }
    if (action == Action::Default) action = defaultAction(event.trigger == Trigger::UpTap, false);
    QuickLockTrigger origin = QuickLockTrigger::None;
    switch (event.trigger) {
      case Trigger::UpTap:
        origin = QuickLockTrigger::SideUpShort;
        break;
      case Trigger::UpHold:
        origin = QuickLockTrigger::SideUpLong;
        break;
      case Trigger::DownTap:
        origin = QuickLockTrigger::SideDownShort;
        break;
      case Trigger::DownHold:
        origin = QuickLockTrigger::SideDownLong;
        break;
      case Trigger::Chord:
        origin = QuickLockTrigger::UpDown;
        break;
      case Trigger::None:
        break;
    }
    return handleReaderShortcut(action, origin);
  }

  return false;
}

void EpubReaderActivity::onReaderGestureStarted() {
  automaticPageTurnActive = false;
  pendingManualTurn = 0;
  pendingManualTurnTouch = false;
}

bool EpubReaderActivity::supportsReaderShortcut(Action action) const {
  switch (action) {
    case Action::ToggleCompletion:
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION) && defined(CROSSPOINT_EXPERIMENTAL_COMPLETION_PROMPT)
      return true;
#else
      return false;
#endif
    case Action::ToggleFrontlight:
    case Action::Brighter:
    case Action::Dimmer:
    case Action::Warmer:
    case Action::Cooler:
    case Action::Home:
    case Action::HomeReader:
    case Action::FileBrowser:
    case Action::FileTransfer:
    case Action::Library:
    case Action::SavedItems:
    case Action::ReadingStatistics:
    case Action::ToggleNight:
    case Action::ToggleTouch:
    case Action::ToggleTilt:
    case Action::TogglePageGestures:
    case Action::JoinNetwork:
    case Action::CreateHotspot:
    case Action::CalibreWireless:
    case Action::ToggleHomeButton:
      return ReaderActivity::supportsReaderShortcut(action);
    case Action::Default:
    case Action::Disabled:
    case Action::QuickActions:
    case Action::QuickLock:
    case Action::SleepOnly:
    case Action::WakeOnly:
    case Action::Count:
      return false;
    case Action::PreviousPage:
    case Action::NextPage:
    case Action::PreviousChapter:
    case Action::NextChapter:
    case Action::SelectChapter:
    case Action::Menu:
    case Action::Bookmark:
    case Action::Dictionary:
    case Action::Footnotes:
    case Action::Refresh:
    case Action::Rotate:
    case Action::RotateBackward:
    case Action::RotateHalfTurn:
    case Action::LargerText:
    case Action::SmallerText:
    case Action::Screenshot:
    case Action::SyncProgress:
    case Action::CreateClipping:
    case Action::FontFamily:
    case Action::CycleFontFamily:
    case Action::ToggleFocus:
    case Action::CycleAutoTurn:
    case Action::Sleep:
      return true;
  }
  return false;
}
