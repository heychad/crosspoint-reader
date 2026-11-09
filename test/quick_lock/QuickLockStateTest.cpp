#include <gtest/gtest.h>

#include <cstdint>

#include "personalization/ButtonShortcutController.h"
#include "personalization/QuickLockState.h"

TEST(QuickLockState, NeverTimeoutDoesNotSleep) {
  QuickLockState state;
  state.toggle(100U);
  EXPECT_FALSE(state.shouldSleep(100000U, 0U));
}

TEST(QuickLockState, TimeoutUsesWraparoundSafeElapsedTime) {
  QuickLockState state;
  state.toggle(UINT32_MAX - 20U);
  EXPECT_FALSE(state.shouldSleep(10U, 40U));
  EXPECT_TRUE(state.shouldSleep(25U, 40U));
}

TEST(ButtonShortcutController, QuickLockChordFiresOnceUntilFullRelease) {
  ButtonShortcutController controller;
  using Action = ButtonShortcutController::ChordAction;
  using Event = ButtonShortcutController::Event;

  EXPECT_EQ(controller.update(10U, true, true, Action::QuickLock).event, Event::QuickLockChanged);
  EXPECT_TRUE(controller.isQuickLocked());
  EXPECT_EQ(controller.update(11U, true, true, Action::QuickLock).event, Event::None);
  EXPECT_TRUE(controller.update(12U, false, false, Action::QuickLock).consumeInput);
  EXPECT_EQ(controller.update(13U, true, true, Action::QuickLock).event, Event::QuickLockChanged);
  EXPECT_FALSE(controller.isQuickLocked());
}

TEST(ButtonShortcutController, LockedStateBlocksOtherChords) {
  ButtonShortcutController controller;
  using Action = ButtonShortcutController::ChordAction;
  using Event = ButtonShortcutController::Event;

  EXPECT_EQ(controller.update(10U, true, true, Action::QuickLock).event, Event::QuickLockChanged);
  controller.update(11U, false, false, Action::QuickLock);
  const auto blocked = controller.update(12U, true, true, Action::Screenshot);
  EXPECT_EQ(blocked.event, Event::None);
  EXPECT_TRUE(blocked.consumeInput);
  controller.update(13U, false, false, Action::Screenshot);
  EXPECT_EQ(controller.update(14U, false, false, Action::Screenshot).event, Event::None);
  EXPECT_TRUE(controller.isQuickLocked());
  EXPECT_EQ(controller.update(15U, true, true, Action::QuickLock).event, Event::QuickLockChanged);
  EXPECT_FALSE(controller.isQuickLocked());
}

TEST(ButtonShortcutController, LongPowerRequiresReleaseBeforeItCanUnlock) {
  ButtonShortcutController controller;

  controller.toggleQuickLock(10U, QuickLockTrigger::LongPower, true);
  EXPECT_TRUE(controller.isQuickLocked());
  EXPECT_FALSE(controller.tryUnlockLongPower(11U, true));
  EXPECT_FALSE(controller.tryUnlockLongPower(12U, false));
  EXPECT_TRUE(controller.tryUnlockLongPower(13U, true));
  EXPECT_FALSE(controller.isQuickLocked());
}

TEST(ButtonShortcutController, HomeQuickLockOnlyUnlocksWithTheMatchingHomeGesture) {
  ButtonShortcutController controller;

  constexpr QuickLockTrigger homeTriggers[] = {QuickLockTrigger::HomeTap, QuickLockTrigger::HomeDoubleTap,
                                               QuickLockTrigger::HomeLongPress};
  for (const auto trigger : homeTriggers) {
    controller.toggleQuickLock(10U, trigger);

    EXPECT_FALSE(controller.tryUnlockWithTrigger(11U, QuickLockTrigger::ShortPower));
    EXPECT_TRUE(controller.isQuickLocked());
    EXPECT_TRUE(controller.tryUnlockWithTrigger(12U, trigger));
    EXPECT_FALSE(controller.isQuickLocked());
  }
}

TEST(ButtonShortcutController, PageTurnChordEmitsPageTurn) {
  ButtonShortcutController controller;
  using Action = ButtonShortcutController::ChordAction;
  using Event = ButtonShortcutController::Event;

  EXPECT_EQ(controller.update(1U, true, true, Action::NextPage).event, Event::PageTurn);
}

TEST(ButtonShortcutController, UpDownChordDispatchesConfiguredAction) {
  ButtonShortcutController controller;
  using Action = ButtonShortcutController::ChordAction;
  using Event = ButtonShortcutController::Event;

  const auto result = controller.updateUpDown(1U, true, true, Action::QuickActions);
  EXPECT_EQ(result.event, Event::ConfiguredAction);
  EXPECT_TRUE(result.consumeInput);
  EXPECT_EQ(result.action, Action::QuickActions);
}

TEST(ButtonShortcutController, IdleUpDownDoesNotPreemptReaderQuickLockUnlock) {
  ButtonShortcutController controller;
  controller.toggleQuickLock(1U, QuickLockTrigger::LongMenu);

  const auto result = controller.updateUpDown(2U, false, false, ButtonShortcutController::ChordAction::Disabled);

  EXPECT_EQ(result.event, ButtonShortcutController::Event::None);
  EXPECT_FALSE(result.consumeInput);
  EXPECT_TRUE(controller.isQuickLocked());
}

TEST(ButtonShortcutController, EveryChordActionConsumesBothReleaseOrders) {
  using Action = ButtonShortcutController::ChordAction;
  for (unsigned value = 0; value < static_cast<unsigned>(Action::Count); ++value) {
    const auto action = static_cast<Action>(value);
    if (action == Action::Default || action == Action::Disabled) continue;
    for (const bool releasePowerFirst : {false, true}) {
      ButtonShortcutController controller;

      EXPECT_NE(controller.update(1U, true, true, action).event, ButtonShortcutController::Event::None);
      const auto firstRelease = controller.update(2U, !releasePowerFirst, releasePowerFirst, action);
      EXPECT_EQ(firstRelease.event, ButtonShortcutController::Event::None);
      EXPECT_TRUE(firstRelease.consumeInput);
      const auto secondRelease = controller.update(3U, false, false, action);
      EXPECT_EQ(secondRelease.event, ButtonShortcutController::Event::None);
      EXPECT_TRUE(secondRelease.consumeInput);
      const auto afterRelease = controller.update(4U, false, false, action);
      EXPECT_EQ(afterRelease.consumeInput, action == Action::QuickLock);
    }
  }
}

TEST(ButtonShortcutController, LibraryUpDownChordFiresOnceAndReleasesBothButtons) {
  using Action = ButtonShortcutController::ChordAction;
  using Event = ButtonShortcutController::Event;

  for (const bool releaseUpFirst : {false, true}) {
    ButtonShortcutController controller;
    const auto started = controller.updateUpDown(1U, true, true, Action::Library);
    EXPECT_EQ(started.event, Event::ConfiguredAction);
    EXPECT_EQ(started.action, Action::Library);
    EXPECT_TRUE(started.consumeInput);

    const auto firstRelease = controller.updateUpDown(2U, !releaseUpFirst, releaseUpFirst, Action::Library);
    EXPECT_EQ(firstRelease.event, Event::None);
    EXPECT_TRUE(firstRelease.consumeInput);
    EXPECT_TRUE(controller.updateUpDown(3U, false, false, Action::Library).consumeInput);
    EXPECT_FALSE(controller.updateUpDown(4U, false, false, Action::Library).consumeInput);
  }
}

TEST(ButtonShortcutController, ModalConsumesPowerUpChordThroughBothReleases) {
  ButtonShortcutController controller;
  using Action = ButtonShortcutController::ChordAction;

  const auto started = controller.update(1U, true, true, Action::QuickLock, true);
  EXPECT_EQ(started.event, ButtonShortcutController::Event::None);
  EXPECT_TRUE(started.consumeInput);
  EXPECT_FALSE(controller.isQuickLocked());
  EXPECT_TRUE(controller.update(2U, false, true, Action::QuickLock, true).consumeInput);
  EXPECT_TRUE(controller.update(3U, false, false, Action::QuickLock, true).consumeInput);
  EXPECT_FALSE(controller.update(4U, false, false, Action::QuickLock, false).consumeInput);
}

TEST(ButtonShortcutController, ModalConsumesPowerDownScreenshotChordThroughBothReleaseOrders) {
  for (const bool releasePowerFirst : {false, true}) {
    ButtonShortcutController controller;
    const auto started = controller.updatePowerDown(true, true, true);
    EXPECT_EQ(started.event, ButtonShortcutController::Event::None);
    EXPECT_TRUE(started.consumeInput);
    EXPECT_TRUE(controller.updatePowerDown(!releasePowerFirst, releasePowerFirst, true).consumeInput);
    EXPECT_TRUE(controller.updatePowerDown(false, false, true).consumeInput);
    EXPECT_FALSE(controller.updatePowerDown(false, false, false).consumeInput);
  }
}

TEST(ButtonShortcutController, QuickLockConsumesPowerDownScreenshotChord) {
  ButtonShortcutController controller;
  controller.toggleQuickLock(1U, QuickLockTrigger::ShortPower);

  const auto result = controller.updatePowerDown(true, true, true);

  EXPECT_EQ(result.event, ButtonShortcutController::Event::None);
  EXPECT_TRUE(result.consumeInput);
  EXPECT_TRUE(controller.isQuickLocked());
}

TEST(ButtonShortcutController, ModalConsumesUpDownChordThroughBothReleaseOrders) {
  using Action = ButtonShortcutController::ChordAction;

  for (const bool releaseUpFirst : {false, true}) {
    ButtonShortcutController controller;
    const auto started = controller.updateUpDown(1U, true, true, Action::QuickActions, true);
    EXPECT_EQ(started.event, ButtonShortcutController::Event::None);
    EXPECT_TRUE(started.consumeInput);
    EXPECT_TRUE(controller.updateUpDown(2U, !releaseUpFirst, releaseUpFirst, Action::QuickActions, true).consumeInput);
    EXPECT_TRUE(controller.updateUpDown(3U, false, false, Action::QuickActions, true).consumeInput);
    EXPECT_FALSE(controller.updateUpDown(4U, false, false, Action::QuickActions, false).consumeInput);
  }
}
