#include "PhysicalShortcuts.h"

#include <HalGPIO.h>

#include "ButtonShortcutController.h"
#include "CrossPointSettings.h"
#include "HoldShortcutController.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "util/ScreenshotUtil.h"

namespace physicalshortcuts {
namespace {
using Action = readershortcuts::Action;
ButtonShortcutController controller;
HoldShortcutController holds;
HoldShortcutController unlockHolds;
bool requireNeutral = false;

QuickLockTrigger homeTrigger(HomeButtonInput::Gesture gesture) {
  switch (gesture) {
    case HomeButtonInput::Gesture::Tap:
      return QuickLockTrigger::HomeTap;
    case HomeButtonInput::Gesture::DoubleTap:
      return QuickLockTrigger::HomeDoubleTap;
    case HomeButtonInput::Gesture::LongPress:
      return QuickLockTrigger::HomeLongPress;
    case HomeButtonInput::Gesture::None:
      return QuickLockTrigger::None;
  }
  return QuickLockTrigger::None;
}
void changed(ActivityManager& manager) {
  requireNeutral = true;
  unlockHolds = {};
  if (!manager.applyInputLock(controller.isQuickLocked()) && controller.isQuickLocked()) {
    controller.toggleQuickLock(0, controller.quickLockTrigger());
  }
}
Outcome takeRequest(ActivityManager& manager, uint32_t now) {
  const auto request = manager.takeControlRequest();
  switch (request.kind) {
    case ActivityManager::ControlKind::Sleep:
      return Outcome::Sleep;
    case ActivityManager::ControlKind::ToggleLock:
      if (controller.isQuickLocked()) {
        if (controller.tryUnlockWithTrigger(now, request.trigger)) {
          changed(manager);
          return Outcome::Handled;
        }
      } else if (manager.canApplyInputLock() && request.trigger != QuickLockTrigger::None) {
        controller.toggleQuickLock(now, request.trigger, request.trigger == QuickLockTrigger::LongPower);
        changed(manager);
        return Outcome::Handled;
      }
      return Outcome::Consumed;
    case ActivityManager::ControlKind::None:
      return Outcome::Pass;
  }
  return Outcome::Pass;
}
Outcome dispatch(ActivityManager& manager, Action action, QuickLockTrigger trigger, uint32_t now) {
  manager.dispatchShortcut(action, trigger);
  const auto requested = takeRequest(manager, now);
  return requested == Outcome::Pass ? Outcome::Handled : requested;
}
HoldShortcutController::Frame frameFor(MappedInputManager& input, HalGPIO& gpio, ActivityManager& manager,
                                       uint32_t now) {
  using Button = MappedInputManager::Button;
  HoldShortcutController::Frame frame;
  frame.now = now;
  frame.visit = manager.visit();
  frame.contacts[0] = {gpio.isPressed(HalGPIO::BTN_POWER), gpio.wasPressed(HalGPIO::BTN_POWER),
                       gpio.wasReleased(HalGPIO::BTN_POWER), static_cast<uint32_t>(gpio.getPowerButtonHeldTime())};
  frame.contacts[1] = {input.isPressed(Button::Back), input.wasPressed(Button::Back), input.wasReleased(Button::Back)};
  frame.contacts[2] = {input.isPressed(Button::Confirm), input.wasPressed(Button::Confirm),
                       input.wasReleased(Button::Confirm)};
  return frame;
}
}  // namespace

bool overridesPowerTap() { return SETTINGS.powerTapAction != static_cast<uint8_t>(Action::Default); }
bool overridesPowerHold() { return SETTINGS.powerHoldAction != static_cast<uint8_t>(Action::Default); }

Outcome update(ActivityManager& manager, MappedInputManager& input, HalGPIO& gpio, GfxRenderer& renderer,
               uint32_t now) {
  auto requested = takeRequest(manager, now);
  if (requested != Outcome::Pass) return requested;
  if (controller.shouldQuickLockSleep(now, SETTINGS.getSleepTimeoutMs())) return Outcome::SleepTimeout;
  const bool power = gpio.isPressed(HalGPIO::BTN_POWER);
  const bool up = gpio.isPressed(HalGPIO::BTN_UP), down = gpio.isPressed(HalGPIO::BTN_DOWN);
  auto frame = frameFor(input, gpio, manager, now);
  HoldShortcutController::Config config;
  auto powerHold = static_cast<Action>(SETTINGS.powerHoldAction);
  if (powerHold == Action::Default && overridesPowerTap()) powerHold = Action::Sleep;
  config.bindings[0] = {static_cast<Action>(SETTINGS.powerTapAction), powerHold, 400};
  if (!input.hasTouch() && manager.isForegroundReader()) {
    config.bindings[1] = {Action::Default, static_cast<Action>(SETTINGS.backHoldAction), 1000};
    config.bindings[2] = {Action::Default, static_cast<Action>(SETTINGS.confirmHoldAction), 400};
  }
  if (requireNeutral) {
    frame.blocked = true;
    holds.update(frame, config);
    requireNeutral = power || up || down || input.isPressed(MappedInputManager::Button::Back) ||
                     input.isPressed(MappedInputManager::Button::Confirm) ||
                     input.isPressed(MappedInputManager::Button::Left) ||
                     input.isPressed(MappedInputManager::Button::Right) || gpio.isHomeKeyDown();
    return Outcome::Consumed;
  }
  const auto screenshot =
      controller.updatePowerDown(power, down, manager.isInputLocked() || controller.isChordActive());
  if (screenshot.consumeInput) {
    frame.blocked = true;
    holds.update(frame, config);
    if (screenshot.event == ButtonShortcutController::Event::Screenshot) {
      RenderLock lock;
      ScreenshotUtil::takeScreenshot(renderer);
      return Outcome::Handled;
    }
    return Outcome::Consumed;
  }
  auto chordAction = static_cast<Action>(SETTINGS.powerUpChordAction);
  if (chordAction == Action::Default) chordAction = Action::Disabled;
  const auto chord =
      controller.update(now, power, up, chordAction, !manager.canApplyInputLock() && chordAction == Action::QuickLock);
  frame.blocked = chord.consumeInput || manager.isInputLocked();
  const auto held = holds.update(frame, config);
  if (chord.event == ButtonShortcutController::Event::QuickLockChanged) {
    changed(manager);
    return Outcome::Handled;
  }
  if (manager.isInputLocked()) {
    if (gpio.wasReleased(HalGPIO::BTN_POWER) && gpio.getPowerButtonHeldTime() < 400 &&
        controller.tryUnlockWithTrigger(now, QuickLockTrigger::ShortPower)) {
      changed(manager);
      return Outcome::Handled;
    }
    if (controller.tryUnlockLongPower(now, power && gpio.getPowerButtonHeldTime() >= 400)) {
      changed(manager);
      return Outcome::Handled;
    }
    const auto sideChord = controller.updateUpDown(
        now, up, down,
        controller.quickLockTrigger() == QuickLockTrigger::UpDown ? Action::QuickLock : Action::Disabled);
    if (sideChord.event == ButtonShortcutController::Event::QuickLockChanged) {
      changed(manager);
      return Outcome::Handled;
    }
    const bool swapped = readershortcuts::swapSideBindings(SETTINGS.readerSideFollowOrientation, SETTINGS.orientation);
    const auto resolvedHold = [&](bool upButton) {
      const bool logicalUp = readershortcuts::logicalSideUp(upButton, swapped);
      const auto action = static_cast<Action>(logicalUp ? SETTINGS.readerUpHold : SETTINGS.readerDownHold);
      return action == Action::Default
                 ? readershortcuts::defaultAction(upButton, true, SETTINGS.sideButtonLayout,
                                                  input.isNavDirectionSwapped(), SETTINGS.longPressButtonBehavior)
                 : action;
    };
    const bool upLong = resolvedHold(true) != Action::Disabled;
    const bool downLong = resolvedHold(false) != Action::Disabled;
    if (controller.tryUnlockSide(now, up, gpio.wasPressed(HalGPIO::BTN_UP), gpio.wasReleased(HalGPIO::BTN_UP), upLong,
                                 down, gpio.wasPressed(HalGPIO::BTN_DOWN), gpio.wasReleased(HalGPIO::BTN_DOWN),
                                 downLong, 700)) {
      changed(manager);
      return Outcome::Handled;
    }
    if (input.homeButtonAction() == HomeButtonAction::QuickLock &&
        controller.tryUnlockWithTrigger(now, homeTrigger(input.homeButtonGesture()))) {
      changed(manager);
      return Outcome::Handled;
    }
    frame.blocked = false;
    HoldShortcutController::Config unlock;
    if (controller.quickLockTrigger() == QuickLockTrigger::LongBack)
      unlock.bindings[1] = {Action::Default, Action::QuickLock, 1000};
    if (controller.quickLockTrigger() == QuickLockTrigger::LongMenu)
      unlock.bindings[2] = {Action::Default, Action::QuickLock, 400};
    const auto unlockEvent = unlockHolds.update(frame, unlock);
    if (unlockEvent.action == Action::QuickLock && controller.tryUnlockWithTrigger(now, unlockEvent.trigger)) {
      changed(manager);
      return Outcome::Handled;
    }
    return Outcome::Consumed;
  }
  if (chord.event == ButtonShortcutController::Event::Screenshot) {
    RenderLock lock;
    ScreenshotUtil::takeScreenshot(renderer);
    return Outcome::Handled;
  }
  if (chord.event == ButtonShortcutController::Event::PageTurn)
    return dispatch(manager, Action::NextPage, QuickLockTrigger::PowerUp, now);
  if (chord.event == ButtonShortcutController::Event::ConfiguredAction)
    return dispatch(manager, chord.action, QuickLockTrigger::PowerUp, now);
  if (chord.consumeInput) return Outcome::Consumed;
  if (held.action != Action::Default) return dispatch(manager, held.action, held.trigger, now);
  if (held.consumed) return Outcome::Consumed;
  if (input.homeButtonAction() == HomeButtonAction::QuickLock)
    return dispatch(manager, Action::QuickLock, homeTrigger(input.homeButtonGesture()), now);
  return Outcome::Pass;
}

void beforeSleep(ActivityManager& manager) {
  if (controller.isQuickLocked()) controller.toggleQuickLock(0, controller.quickLockTrigger());
  manager.applyInputLock(false, false);
  controller = {};
  holds = {};
  unlockHolds = {};
  requireNeutral = false;
}
}  // namespace physicalshortcuts
