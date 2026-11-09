#include <gtest/gtest.h>

#include "personalization/ReaderQuickBindings.h"
using namespace readerquickactions;
TEST(QuickBindings, HomeAssignmentReplacesSideWithoutChangingOtherCommands) {
  CrossPointSettings s;
  s.readerUpTap = QUICK;
  s.readerDownTap = 3;
  s.homeButtonTapAction = static_cast<uint8_t>(HomeButtonAction::QuickActions);
  EXPECT_TRUE(normalizeBindings(s, &CrossPointSettings::homeButtonTapAction));
  EXPECT_EQ(s.readerUpTap, 0);
  EXPECT_EQ(s.readerDownTap, 3);
  EXPECT_EQ(s.homeButtonTapAction, static_cast<uint8_t>(HomeButtonAction::QuickActions));
}
TEST(QuickBindings, ReplacingHomeTriggerRestoresItsNormalAction) {
  CrossPointSettings s;
  s.homeButtonDoubleTapAction = static_cast<uint8_t>(HomeButtonAction::QuickActions);
  s.shortPwrBtn = CrossPointSettings::PWR_QUICK_ACTIONS;
  normalizeBindings(s, &CrossPointSettings::shortPwrBtn);
  EXPECT_EQ(s.homeButtonDoubleTapAction, static_cast<uint8_t>(HomeButtonAction::ToggleFrontlight));
  EXPECT_EQ(s.shortPwrBtn, CrossPointSettings::PWR_QUICK_ACTIONS);
}
TEST(QuickBindings, LoadingNewPowerTriggerTakesOwnershipFromExistingSide) {
  CrossPointSettings s;
  s.readerSideChord = QUICK;
  const auto before = bindingValues(s);
  s.shortPwrBtn = CrossPointSettings::PWR_QUICK_ACTIONS;
  EXPECT_TRUE(normalizeChangedBindings(s, before));
  EXPECT_EQ(s.readerSideChord, 0);
  EXPECT_EQ(s.shortPwrBtn, CrossPointSettings::PWR_QUICK_ACTIONS);
}
TEST(QuickBindings, UnrelatedSettingsDoNotCreateOrClearCommands) {
  CrossPointSettings s;
  s.readerUpTap = 4;
  s.readerDownHold = 8;
  const auto before = bindingValues(s);
  EXPECT_FALSE(normalizeChangedBindings(s, before));
  EXPECT_EQ(s.readerUpTap, 4);
  EXPECT_EQ(s.readerDownHold, 8);
}

TEST(QuickBindings, ReaderActionNumberMatchingHomeQuickActionIsNotATrigger) {
  CrossPointSettings s;
  s.readerUpTap = static_cast<uint8_t>(HomeButtonAction::QuickActions);
  s.homeButtonTapAction = static_cast<uint8_t>(HomeButtonAction::QuickActions);
  EXPECT_FALSE(normalizeBindings(s, &CrossPointSettings::homeButtonTapAction));
  EXPECT_EQ(s.readerUpTap, static_cast<uint8_t>(HomeButtonAction::QuickActions));
}

TEST(QuickBindings, EveryPhysicalBindingParticipatesInSingleTriggerOwnership) {
  const std::array<uint8_t CrossPointSettings::*, 5> fields = {
      &CrossPointSettings::powerTapAction, &CrossPointSettings::powerHoldAction,
      &CrossPointSettings::powerUpChordAction, &CrossPointSettings::backHoldAction,
      &CrossPointSettings::confirmHoldAction};
  for (auto field : fields) {
    CrossPointSettings s;
    s.readerUpTap = QUICK;
    auto before = bindingValues(s);
    s.*field = QUICK;
    EXPECT_TRUE(normalizeChangedBindings(s, before));
    EXPECT_EQ(s.readerUpTap, 0);
    EXPECT_EQ(s.*field, QUICK);
  }
}
