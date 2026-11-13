#include <gtest/gtest.h>

#include "CrossPointSettings.h"
#include "HalGPIO.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "personalization/PhysicalShortcuts.h"
CrossPointSettings settings;
using A = readershortcuts::Action;
using O = physicalshortcuts::Outcome;
class PhysicalShortcuts : public ::testing::Test {
 protected:
  ActivityManager manager;
  MappedInputManager input;
  HalGPIO gpio;
  GfxRenderer renderer;
  uint32_t now = 0;
  void SetUp() override {
    settings = {};
    physicalshortcuts::beforeSleep(manager);
  }
  O tick() { return physicalshortcuts::update(manager, input, gpio, renderer, now); }
  void neutral() {
    gpio.down = {};
    gpio.pressed = {};
    gpio.released = {};
    input.down = {};
    input.pressed = {};
    input.released = {};
    input.home = HomeButtonAction::Ignore;
    input.gesture = HomeButtonInput::Gesture::None;
    tick();
    tick();
  }
  void power(bool down, bool pressed, bool released, uint32_t held = 0) {
    gpio.down[0] = down;
    gpio.pressed[0] = pressed;
    gpio.released[0] = released;
    gpio.held = held;
  }
};
TEST_F(PhysicalShortcuts, UnconfiguredPowerIsLeftToLegacyBehavior) {
  power(true, true, false);
  EXPECT_EQ(tick(), O::Pass);
  now = 500;
  power(true, false, false, 500);
  EXPECT_EQ(tick(), O::Pass);
  power(false, false, true, 500);
  EXPECT_EQ(tick(), O::Pass);
}
TEST_F(PhysicalShortcuts, PowerTapLocksAndNeedsFreshMatchingRelease) {
  settings.powerTapAction = static_cast<uint8_t>(A::QuickLock);
  power(true, true, false);
  EXPECT_EQ(tick(), O::Consumed);
  now = 50;
  power(false, false, true, 50);
  EXPECT_EQ(tick(), O::Handled);
  EXPECT_TRUE(manager.locked);
  EXPECT_EQ(tick(), O::Consumed);
  EXPECT_TRUE(manager.locked);
  neutral();
  power(true, true, false);
  tick();
  now = 100;
  power(false, false, true, 50);
  EXPECT_EQ(tick(), O::Handled);
  EXPECT_FALSE(manager.locked);
  EXPECT_EQ(tick(), O::Consumed);
}
TEST_F(PhysicalShortcuts, LongPowerLockCannotImmediatelyUnlock) {
  settings.powerHoldAction = static_cast<uint8_t>(A::QuickLock);
  power(true, true, false);
  tick();
  now = 400;
  power(true, false, false, 400);
  EXPECT_EQ(tick(), O::Handled);
  EXPECT_TRUE(manager.locked);
  now = 900;
  EXPECT_EQ(tick(), O::Consumed);
  EXPECT_TRUE(manager.locked);
  neutral();
  tick();
  power(true, true, false);
  tick();
  power(true, false, false, 400);
  now = 1400;
  EXPECT_EQ(tick(), O::Handled);
  EXPECT_FALSE(manager.locked);
}
TEST_F(PhysicalShortcuts, CustomTapKeepsSleepOnLongPower) {
  settings.powerTapAction = static_cast<uint8_t>(A::QuickActions);
  power(true, true, false);
  tick();
  now = 400;
  power(true, false, false, 400);
  EXPECT_EQ(tick(), O::Sleep);
}
TEST_F(PhysicalShortcuts, PowerDownScreenshotOwnsTripleChordAndBothReleaseOrders) {
  settings.powerUpChordAction = static_cast<uint8_t>(A::QuickLock);
  gpio.down = {true, true, true};
  EXPECT_EQ(tick(), O::Handled);
  EXPECT_EQ(renderer.screenshots, 1);
  EXPECT_FALSE(manager.locked);
  gpio.down = {false, true, true};
  EXPECT_EQ(tick(), O::Consumed);
  gpio.down = {false, false, false};
  EXPECT_EQ(tick(), O::Consumed);
  EXPECT_EQ(tick(), O::Pass);
  gpio.down = {true, false, true};
  EXPECT_EQ(tick(), O::Handled);
  gpio.down = {true, false, false};
  EXPECT_EQ(tick(), O::Consumed);
  gpio.down = {};
  EXPECT_EQ(tick(), O::Consumed);
  EXPECT_EQ(renderer.screenshots, 2);
}
TEST_F(PhysicalShortcuts, PowerUpLockOnlyUnlocksOnItsChord) {
  settings.powerUpChordAction = static_cast<uint8_t>(A::QuickLock);
  gpio.down = {true, true, false};
  EXPECT_EQ(tick(), O::Handled);
  EXPECT_TRUE(manager.locked);
  neutral();
  power(true, true, false);
  tick();
  power(false, false, true, 20);
  tick();
  EXPECT_TRUE(manager.locked);
  neutral();
  gpio.down = {true, true, false};
  EXPECT_EQ(tick(), O::Handled);
  EXPECT_FALSE(manager.locked);
}
TEST_F(PhysicalShortcuts, SideLockRejectsOtherOriginAndConsumesUnlockTail) {
  manager.pending = {ActivityManager::ControlKind::ToggleLock, QuickLockTrigger::SideUpLong};
  tick();
  neutral();
  gpio.down[2] = true;
  gpio.pressed[2] = true;
  tick();
  gpio.pressed[2] = false;
  now = 900;
  tick();
  EXPECT_TRUE(manager.locked);
  neutral();
  settings.readerUpHold = static_cast<uint8_t>(A::QuickLock);
  gpio.down[1] = true;
  gpio.pressed[1] = true;
  tick();
  gpio.pressed[1] = false;
  now = 1600;
  EXPECT_EQ(tick(), O::Handled);
  EXPECT_FALSE(manager.locked);
  EXPECT_EQ(tick(), O::Consumed);
}
TEST_F(PhysicalShortcuts, HomeDoubleTapRequiresSameGesture) {
  input.home = HomeButtonAction::QuickLock;
  input.gesture = HomeButtonInput::Gesture::DoubleTap;
  EXPECT_EQ(tick(), O::Handled);
  neutral();
  input.home = HomeButtonAction::QuickLock;
  input.gesture = HomeButtonInput::Gesture::Tap;
  tick();
  EXPECT_TRUE(manager.locked);
  input.gesture = HomeButtonInput::Gesture::DoubleTap;
  EXPECT_EQ(tick(), O::Handled);
  EXPECT_FALSE(manager.locked);
}

TEST_F(PhysicalShortcuts, SideHoldUnlockFollowsReaderOrientationWithAsymmetricBindings) {
  struct Mapping {
    bool follows;
    uint8_t orientation;
    bool logicalUpIsPhysicalUp;
  };
  const Mapping mappings[] = {{false, 0, true}, {false, 1, true}, {false, 2, true}, {false, 3, true},
                              {true, 0, true},  {true, 1, false}, {true, 2, false}, {true, 3, false}};
  for (const auto mapping : mappings) {
    for (const bool quickLockOnUp : {false, true}) {
      SCOPED_TRACE(::testing::Message() << mapping.follows << '/' << unsigned(mapping.orientation) << '/'
                                        << quickLockOnUp);
      physicalshortcuts::beforeSleep(manager);
      manager = {};
      input = {};
      gpio = {};
      settings = {};
      now = 0;
      settings.readerSideFollowOrientation = mapping.follows;
      settings.orientation = mapping.orientation;
      settings.readerUpHold = static_cast<uint8_t>(quickLockOnUp ? A::QuickLock : A::Disabled);
      settings.readerDownHold = static_cast<uint8_t>(quickLockOnUp ? A::Disabled : A::QuickLock);
      const bool physicalUp = quickLockOnUp == mapping.logicalUpIsPhysicalUp;
      const unsigned matching = physicalUp ? HalGPIO::BTN_UP : HalGPIO::BTN_DOWN;
      const unsigned other = physicalUp ? HalGPIO::BTN_DOWN : HalGPIO::BTN_UP;
      manager.pending = {ActivityManager::ControlKind::ToggleLock,
                         physicalUp ? QuickLockTrigger::SideUpLong : QuickLockTrigger::SideDownLong};
      EXPECT_EQ(tick(), O::Handled);
      ASSERT_TRUE(manager.locked);
      neutral();
      gpio.down[other] = gpio.pressed[other] = true;
      tick();
      gpio.pressed[other] = false;
      now = 800;
      tick();
      EXPECT_TRUE(manager.locked);
      neutral();
      gpio.down[matching] = gpio.pressed[matching] = true;
      tick();
      gpio.pressed[matching] = false;
      now = 1600;
      EXPECT_EQ(tick(), O::Handled);
      EXPECT_FALSE(manager.locked);
      EXPECT_EQ(tick(), O::Consumed);
    }
  }
}
TEST_F(PhysicalShortcuts, FrontHoldUsesLogicalRemapAndDoesNotLeakRelease) {
  input.touch = false;
  settings.backHoldAction = static_cast<uint8_t>(A::QuickLock);
  input.down[0] = input.pressed[0] = true;
  tick();
  now = 1000;
  input.pressed[0] = false;
  EXPECT_EQ(tick(), O::Handled);
  EXPECT_TRUE(manager.locked);
  neutral();
  input.down[0] = input.pressed[0] = true;
  tick();
  now = 2100;
  input.pressed[0] = false;
  EXPECT_EQ(tick(), O::Handled);
  EXPECT_FALSE(manager.locked);
  input.down[0] = false;
  input.released[0] = true;
  EXPECT_EQ(tick(), O::Consumed);
}
TEST_F(PhysicalShortcuts, LockedInputCannotPreventTimeout) {
  manager.pending = {ActivityManager::ControlKind::ToggleLock, QuickLockTrigger::HomeTap};
  tick();
  neutral();
  gpio.down[1] = gpio.pressed[1] = true;
  now = 10001;
  EXPECT_EQ(tick(), O::SleepTimeout);
}
TEST_F(PhysicalShortcuts, BusyActivitiesRejectLock) {
  manager.canLock = false;
  manager.pending = {ActivityManager::ControlKind::ToggleLock, QuickLockTrigger::HomeTap};
  tick();
  EXPECT_FALSE(manager.locked);
  settings.powerUpChordAction = static_cast<uint8_t>(A::QuickLock);
  gpio.down = {true, true, false};
  EXPECT_EQ(tick(), O::Consumed);
  EXPECT_FALSE(manager.locked);
}
TEST_F(PhysicalShortcuts, PowerUpScreenshotIsDispatchedOnce) {
  settings.powerUpChordAction = static_cast<uint8_t>(A::Screenshot);
  gpio.down = {true, true, false};
  EXPECT_EQ(tick(), O::Handled);
  EXPECT_EQ(tick(), O::Consumed);
  EXPECT_EQ(renderer.screenshots, 1);
}

TEST_F(PhysicalShortcuts, TimeoutStillFiresWhileLockContactIsHeld) {
  settings.powerHoldAction = static_cast<uint8_t>(A::QuickLock);
  power(true, true, false);
  tick();
  now = 400;
  power(true, false, false, 400);
  tick();
  now = 10400;
  EXPECT_EQ(tick(), O::SleepTimeout);
}

TEST_F(PhysicalShortcuts, PowerUpOwnerBlocksLaterScreenshotUntilFullRelease) {
  settings.powerUpChordAction = static_cast<uint8_t>(A::NextPage);
  gpio.down = {true, true, false};
  EXPECT_EQ(tick(), O::Handled);
  EXPECT_EQ(manager.dispatched, 1);
  gpio.down[2] = true;
  EXPECT_EQ(tick(), O::Consumed);
  EXPECT_EQ(renderer.screenshots, 0);
  gpio.down = {false, true, false};
  tick();
  gpio.down = {};
  tick();
  tick();
  gpio.down = {true, false, true};
  EXPECT_EQ(tick(), O::Handled);
  EXPECT_EQ(renderer.screenshots, 1);
}

TEST_F(PhysicalShortcuts, ReaderHoldsPreemptLegacyTimersWithOffsetPollClocks) {
  input.touch = false;
  for (int index : {0, 1}) {
    physicalshortcuts::beforeSleep(manager);
    input.down = {};
    input.pressed = {};
    input.released = {};
    input.held = 30;
    settings.backHoldAction = static_cast<uint8_t>(A::QuickLock);
    settings.confirmHoldAction = static_cast<uint8_t>(A::QuickLock);
    const uint32_t threshold = index == 0 ? 1000 : 400;
    now = 100;
    input.down[index] = input.pressed[index] = true;
    EXPECT_EQ(tick(), O::Consumed);  // A legacy reader cannot see this press.
    now = 100 + threshold - 10;
    input.pressed[index] = false;
    input.held = threshold + 20;
    EXPECT_EQ(tick(), O::Consumed);
    EXPECT_FALSE(manager.locked);
    now += 20;
    EXPECT_EQ(tick(), O::Handled);
    EXPECT_TRUE(manager.locked);
    input.down[index] = false;
    input.released[index] = true;
    EXPECT_EQ(tick(), O::Consumed);
  }
}

TEST_F(PhysicalShortcuts, ShortReaderBackStillReachesItsReleaseHandler) {
  input.touch = false;
  settings.backHoldAction = static_cast<uint8_t>(A::QuickLock);
  input.down[0] = input.pressed[0] = true;
  EXPECT_EQ(tick(), O::Consumed);
  now = 100;
  input.held = 100;
  input.down[0] = input.pressed[0] = false;
  input.released[0] = true;
  EXPECT_EQ(tick(), O::Pass);
  EXPECT_FALSE(manager.locked);
}

TEST_F(PhysicalShortcuts, FrontOverridesLeaveNonReaderScreensUsable) {
  input.touch = false;
  manager.reader = false;
  settings.backHoldAction = static_cast<uint8_t>(A::QuickLock);
  input.down[0] = input.pressed[0] = true;
  EXPECT_EQ(tick(), O::Pass);
  now = 1200;
  input.held = 1200;
  input.pressed[0] = false;
  EXPECT_EQ(tick(), O::Pass);
  EXPECT_FALSE(manager.locked);
}

TEST_F(PhysicalShortcuts, DisabledDefaultSideHoldHasTheSameMeaningForLockAndUnlock) {
  settings.sideButtonLayout = 2;
  settings.longPressButtonBehavior = 1;
  for (bool up : {true, false}) {
    physicalshortcuts::beforeSleep(manager);
    neutral();
    const bool hasHold = readershortcuts::defaultAction(up, true, settings.sideButtonLayout, false,
                                                        settings.longPressButtonBehavior) != A::Disabled;
    ASSERT_FALSE(hasHold);
    readershortcuts::Tracker tracker;
    std::array<readershortcuts::Contact, 2> contacts{};
    int i = up ? 0 : 1;
    contacts[i] = {true, true, false};
    tracker.update(0, contacts, true, {hasHold, hasHold}, false);
    contacts[i] = {false, false, true};
    auto event = tracker.update(800, contacts, true, {hasHold, hasHold}, false);
    ASSERT_EQ(event.trigger, up ? readershortcuts::Trigger::UpTap : readershortcuts::Trigger::DownTap);
    manager.pending = {ActivityManager::ControlKind::ToggleLock,
                       up ? QuickLockTrigger::SideUpShort : QuickLockTrigger::SideDownShort};
    now = 800;
    tick();
    ASSERT_TRUE(manager.locked);
    neutral();
    int raw = up ? HalGPIO::BTN_UP : HalGPIO::BTN_DOWN;
    gpio.down[raw] = gpio.pressed[raw] = true;
    tick();
    now = 1600;
    gpio.pressed[raw] = false;
    tick();
    ASSERT_TRUE(manager.locked);
    gpio.down[raw] = false;
    gpio.released[raw] = true;
    EXPECT_EQ(tick(), O::Handled);
    EXPECT_FALSE(manager.locked);
  }
}

TEST_F(PhysicalShortcuts, AnotherHeldButtonCannotAccelerateFrontLockOrUnlock) {
  input.touch = false;
  for (int index : {0, 1}) {
    physicalshortcuts::beforeSleep(manager);
    neutral();
    settings.backHoldAction = static_cast<uint8_t>(A::QuickLock);
    settings.confirmHoldAction = static_cast<uint8_t>(A::QuickLock);
    const uint32_t threshold = index == 0 ? 1000 : 400;
    for (bool locking : {true, false}) {
      const uint32_t started = now;
      gpio.down[1] = true;
      input.held = 5000;
      tick();
      input.down[index] = input.pressed[index] = true;
      EXPECT_EQ(tick(), O::Consumed);
      EXPECT_EQ(manager.locked, !locking);
      now = started + 10;
      input.down[index] = input.pressed[index] = false;
      input.released[index] = true;
      tick();
      EXPECT_EQ(manager.locked, !locking);
      input.released[index] = false;
      input.down[index] = input.pressed[index] = true;
      now += 10;
      tick();
      input.pressed[index] = false;
      now += threshold;
      EXPECT_EQ(tick(), O::Handled);
      EXPECT_EQ(manager.locked, locking);
      neutral();
    }
  }
}
