#include <gtest/gtest.h>

#include <limits>

#include "personalization/ReaderBarsJson.h"
#include "personalization/ReaderPageLayout.h"
#include "personalization/ReaderShortcuts.h"

TEST(ReaderBars, RoundTripAndRejectMalformedSettingsAtomically) {
  ReaderBar original;
  original.slots = {ReaderBarItem::Clock,       ReaderBarItem::Date,         ReaderBarItem::Battery,
                    ReaderBarItem::BookTitle,   ReaderBarItem::ChapterPages, ReaderBarItem::BookPercent,
                    ReaderBarItem::ChapterTitle};
  original.progress = 1;
  original.thickness = 2;
  original.percentageDecimals = 2;
  JsonDocument doc;
  writeReaderBar(doc.to<JsonObject>(), original);
  ReaderBar result;
  ASSERT_TRUE(readReaderBar(doc.as<JsonVariantConst>(), result));
  EXPECT_EQ(result.slots, original.slots);
  EXPECT_EQ(result.progress, 1);
  EXPECT_EQ(result.thickness, 2);
  EXPECT_EQ(result.percentageDecimals, 2);
  doc["slots"][2] = -1;
  EXPECT_FALSE(readReaderBar(doc.as<JsonVariantConst>(), result));
  EXPECT_EQ(result.slots, original.slots);
  doc["slots"][2] = static_cast<unsigned>(ReaderBarItem::Count);
  EXPECT_FALSE(readReaderBar(doc.as<JsonVariantConst>(), result));
  doc["slots"][2] = 2;
  doc["progress"] = 3;
  EXPECT_FALSE(readReaderBar(doc.as<JsonVariantConst>(), result));
  doc["progress"] = "0";
  EXPECT_FALSE(readReaderBar(doc.as<JsonVariantConst>(), result));
  doc["progress"] = 0;
  doc["slots"].as<JsonArray>().remove(6);
  EXPECT_FALSE(readReaderBar(doc.as<JsonVariantConst>(), result));
}

TEST(ReaderBars, HeightAndUnavailableClock) {
  ReaderBar bar;
  EXPECT_EQ(bar.height(24, 2), 0);
  bar.slots[0] = ReaderBarItem::Clock;
  EXPECT_TRUE(bar.hasText());
  EXPECT_FALSE(bar.hasText(false));
  EXPECT_EQ(bar.height(24, 2), 24);
  bar.progress = 0;
  bar.thickness = 2;
  EXPECT_EQ(bar.height(24, 2), 32);
  bar.slots = {};
  EXPECT_EQ(bar.height(24, 2), 8);
}

TEST(ReaderBars, TimeLeftSlotsRoundTripWithoutChangingOlderIds) {
  EXPECT_EQ(static_cast<unsigned>(ReaderBarItem::Date), 7U);
  ReaderBar original;
  original.slots[0] = ReaderBarItem::TimeLeftBook;
  original.slots[1] = ReaderBarItem::TimeLeftSection;
  JsonDocument doc;
  writeReaderBar(doc.to<JsonObject>(), original);
  EXPECT_EQ(doc["slots"][0].as<unsigned>(), 8U);
  EXPECT_EQ(doc["slots"][1].as<unsigned>(), 9U);
  ReaderBar restored;
  ASSERT_TRUE(readReaderBar(doc.as<JsonVariantConst>(), restored));
  EXPECT_EQ(restored.slots, original.slots);
}

TEST(ReaderBars, CrowdedSideClustersFitAndLeaveNoNegativeCenter) {
  for (int width : {0, 10, 40, 200, 448, 768}) {
    std::array<int, 7> widths{300, 100, 20, 600, 50, 180, 400};
    const auto layout = layoutReaderBar(widths, 0, width, 12);
    EXPECT_GE(layout.centerLeft, 0);
    EXPECT_LE(layout.centerRight, width);
    EXPECT_LE(layout.centerLeft, layout.centerRight);
    for (unsigned i = 0; i < widths.size(); ++i) {
      if (i == ReaderBar::CENTER || !widths[i]) continue;
      EXPECT_GE(layout.x[i], 0);
      EXPECT_LE(layout.x[i] + widths[i], width);
    }
  }
}

TEST(ReaderShortcuts, TapHoldAndInheritedRelease) {
  using namespace readershortcuts;
  Tracker t;
  EXPECT_EQ(t.update(0, {{{true, false, false}, {}}}, true, {true, true}, false).trigger, Trigger::None);
  EXPECT_EQ(t.update(20, {{{false, false, true}, {}}}, true, {true, true}, false).trigger, Trigger::None);
  EXPECT_EQ(t.update(30, {{{true, true, false}, {}}}, true, {true, true}, false).trigger, Trigger::None);
  EXPECT_EQ(t.update(100, {{{false, false, true}, {}}}, true, {true, true}, false).trigger, Trigger::UpTap);
  t.update(200, {{{true, true, false}, {}}}, true, {true, true}, false);
  EXPECT_EQ(t.update(900, {{{true, false, false}, {}}}, true, {true, true}, false).trigger, Trigger::UpHold);
  EXPECT_EQ(t.update(1100, {{{false, false, true}, {}}}, true, {true, true}, false).trigger, Trigger::None);
}

TEST(ReaderShortcuts, ChordConsumesUntilBothReleasedAndResetsWhenModal) {
  using namespace readershortcuts;
  Tracker t;
  EXPECT_EQ(t.update(0, {{{true, true, false}, {true, true, false}}}, true, {true, true}, true).trigger,
            Trigger::Chord);
  EXPECT_EQ(t.update(800, {{{false, false, true}, {true, false, false}}}, true, {true, true}, true).trigger,
            Trigger::None);
  EXPECT_EQ(t.update(900, {{{}, {false, false, true}}}, true, {true, true}, true).trigger, Trigger::None);
  t.update(1000, {{{true, true, false}, {}}}, true, {true, true}, false);
  t.update(1010, {{{true, false, false}, {}}}, false, {true, true}, false);
  EXPECT_EQ(t.update(1100, {{{false, false, true}, {}}}, true, {true, true}, false).trigger, Trigger::None);
}

TEST(ReaderShortcuts, TimerWrapAndDefaultOrientation) {
  using namespace readershortcuts;
  Tracker t;
  t.update(UINT32_MAX - 500, {{{}, {true, true, false}}}, true, {true, true}, false);
  EXPECT_EQ(t.update(300, {{{}, {true, false, false}}}, true, {true, true}, false).trigger, Trigger::DownHold);
  EXPECT_EQ(pageAction(true, 0, false), Action::PreviousPage);
  EXPECT_EQ(pageAction(true, 0, true), Action::NextPage);
  EXPECT_EQ(pageAction(false, 1, false), Action::PreviousPage);
  EXPECT_EQ(pageAction(true, 2, false), Action::Disabled);
  EXPECT_EQ(pageAction(true, 3, true), Action::NextPage);
  EXPECT_EQ(pageAction(false, 4, false), Action::PreviousPage);
}

TEST(ReaderShortcuts, InheritedContactCannotStartChord) {
  using namespace readershortcuts;
  Tracker t;
  EXPECT_EQ(t.update(0, {{{true, false, false}, {true, true, false}}}, true, {false, false}, true).trigger,
            Trigger::None);
}

TEST(ReaderShortcuts, ConsumedContactCannotNavigateNewOwner) {
  SideButtonContactGuard guard;
  guard.consume(true, false);
  EXPECT_TRUE(guard.blocks(true));
  guard.update(true, false, false, false);
  EXPECT_TRUE(guard.blocks(true));
  guard.update(false, false, true, false);
  EXPECT_TRUE(guard.blocks(true));
  guard.update(false, false, false, false);
  EXPECT_FALSE(guard.blocks(true));
  guard.consume(true, true);
  guard.update(false, true, true, false);
  EXPECT_TRUE(guard.blocks(true));
  EXPECT_TRUE(guard.blocks(false));
  guard.update(false, true, false, false);
  EXPECT_TRUE(guard.blocks(true));
  EXPECT_TRUE(guard.blocks(false));
  guard.update(false, false, false, true);
  EXPECT_TRUE(guard.blocks(false));
  guard.update(false, false, false, false);
  EXPECT_FALSE(guard.blocks(false));
}

TEST(ReaderShortcuts, DefaultHoldPreservesDirectionalRotation) {
  using namespace readershortcuts;
  EXPECT_EQ(defaultAction(true, true, 0, false, 2), Action::Rotate);
  EXPECT_EQ(defaultAction(false, true, 0, false, 2), Action::RotateBackward);
  EXPECT_EQ(defaultAction(true, true, 0, true, 2), Action::RotateBackward);
  EXPECT_EQ(defaultAction(false, true, 0, true, 2), Action::Rotate);
  EXPECT_EQ(defaultAction(false, true, 2, false, 2), Action::Disabled);
  EXPECT_EQ(defaultAction(false, true, 0, false, 1), Action::NextChapter);
}

TEST(ReaderLayout, BarAndSelectionShareOriginInEveryOrientation) {
  for (auto dims : {std::array<int, 2>{480, 800}, std::array<int, 2>{800, 480}}) {
    for (auto insets : {std::array<int, 4>{1, 2, 3, 4}, std::array<int, 4>{3, 4, 1, 2}}) {
      const auto layout =
          makeReaderPageLayout(dims[0], dims[1], insets[0], insets[1], insets[2], insets[3], 10, 30, 24);
      EXPECT_EQ(layout.top, insets[0] + 30);
      EXPECT_EQ(layout.bottom, insets[2] + 24);
      EXPECT_EQ(layout.left, insets[3] + 10);
      EXPECT_EQ(layout.right, insets[1] + 10);
      EXPECT_EQ(layout.height + layout.top + layout.bottom, dims[1]);
      EXPECT_EQ(layout.width + layout.left + layout.right, dims[0]);
    }
  }
}

TEST(ReaderBars, TransientNoticeUsesReservedTitleLane) {
  ReaderBar top, bottom;
  top.slots[3] = ReaderBarItem::BookTitle;
  EXPECT_EQ(transientReaderBar(top, bottom, false), ReaderBarPosition::Top);
  EXPECT_EQ(transientReaderBar(top, bottom, true), ReaderBarPosition::Bottom);
  bottom.slots[3] = ReaderBarItem::ChapterTitle;
  EXPECT_EQ(transientReaderBar(top, bottom, false), ReaderBarPosition::Bottom);
}

TEST(ReaderShortcuts, StaggeredHoldsKeepIndependentActions) {
  using namespace readershortcuts;
  Tracker tracker;
  tracker.update(0, {{{true, true, false}, {}}}, true, {true, true}, false);
  tracker.update(400, {{{true, false, false}, {true, true, false}}}, true, {true, true}, false);
  EXPECT_EQ(tracker.update(700, {{{true, false, false}, {true, false, false}}}, true, {true, true}, false).trigger,
            Trigger::UpHold);
  EXPECT_EQ(tracker.update(1100, {{{}, {true, false, false}}}, true, {true, true}, false).trigger, Trigger::DownHold);
}

#include "personalization/ReaderQuickActions.h"
TEST(ReaderQuickActions, FiveSlotsRoundTripWithoutTriggerRecursion) {
  using namespace readerquickactions;
  Slots slots{static_cast<uint8_t>(Action::Menu), static_cast<uint8_t>(Action::Brighter), 0, 0, 0};
  JsonDocument doc;
  write(doc, slots);
  Slots restored{};
  EXPECT_FALSE(read(doc.as<JsonVariantConst>(), restored));
  EXPECT_EQ(slots, restored);
  doc["readerQuickActionsV1"][2] = static_cast<uint8_t>(Action::QuickActions);
  doc["readerQuickActionsV1"][3] = 255;
  doc["readerQuickActionsV1"][4] = "bad";
  EXPECT_TRUE(read(doc.as<JsonVariantConst>(), restored));
  EXPECT_EQ(restored[2], 0);
  EXPECT_EQ(restored[3], 0);
  EXPECT_EQ(restored[4], 0);
}
TEST(ReaderQuickActions, PartialSlotsArePaddedAndMissingDocumentPreservesDefaults) {
  using namespace readerquickactions;
  Slots slots{2, 3, 4, 5, 6};
  JsonDocument doc;
  EXPECT_FALSE(read(doc.as<JsonVariantConst>(), slots));
  EXPECT_EQ(slots[4], 6);
  doc["readerQuickActionsV1"].to<JsonArray>().add(2);
  EXPECT_TRUE(read(doc.as<JsonVariantConst>(), slots));
  EXPECT_EQ(slots, (Slots{2, 0, 0, 0, 0}));
}
TEST(ReaderQuickActions, OneTriggerOwnsTheMenuAndEditedBindingWins) {
  using namespace readerquickactions;
  const auto menu = static_cast<uint8_t>(Action::QuickActions);
  std::array<uint8_t, 5> bindings{menu, 2, menu, 3, menu};
  EXPECT_TRUE(normalizeTriggers(bindings, 4));
  EXPECT_EQ(bindings, (std::array<uint8_t, 5>{0, 2, 0, 3, menu}));
  EXPECT_FALSE(normalizeTriggers(bindings));
}

TEST(ReaderQuickActions, SavedItemsHasANewStableValueAndRoundTrips) {
  using namespace readerquickactions;
  EXPECT_EQ(static_cast<uint8_t>(Action::Sleep), 35);
  EXPECT_EQ(static_cast<uint8_t>(Action::SavedItems), 36);
  EXPECT_TRUE(isSlotAction(static_cast<uint8_t>(Action::SavedItems)));
  Slots slots{static_cast<uint8_t>(Action::SavedItems), 0, 0, 0, 0};
  JsonDocument doc;
  write(doc, slots);
  Slots restored{};
  EXPECT_FALSE(read(doc.as<JsonVariantConst>(), restored));
  EXPECT_EQ(restored, slots);
}

TEST(ReaderQuickActions, FileTransferHasAStableValueAndRoundTrips) {
  using namespace readerquickactions;
  EXPECT_EQ(static_cast<uint8_t>(Action::SavedItems), 36);
  EXPECT_EQ(static_cast<uint8_t>(Action::FileTransfer), 37);
  EXPECT_TRUE(isSlotAction(static_cast<uint8_t>(Action::FileTransfer)));
  Slots slots{static_cast<uint8_t>(Action::FileTransfer), 0, 0, 0, 0};
  JsonDocument doc;
  write(doc, slots);
  Slots restored{};
  EXPECT_FALSE(read(doc.as<JsonVariantConst>(), restored));
  EXPECT_EQ(restored, slots);
}

TEST(ReaderQuickActions, FontFamilyAppendsWithoutChangingExistingValues) {
  using namespace readerquickactions;
  EXPECT_EQ(static_cast<uint8_t>(Action::FileTransfer), 37);
  EXPECT_EQ(static_cast<uint8_t>(Action::FontFamily), 38);
  EXPECT_TRUE(isSlotAction(static_cast<uint8_t>(Action::FontFamily)));
  Slots slots{static_cast<uint8_t>(Action::FontFamily), 0, 0, 0, 0};
  JsonDocument doc;
  write(doc, slots);
  Slots restored{};
  EXPECT_FALSE(read(doc.as<JsonVariantConst>(), restored));
  EXPECT_EQ(restored, slots);
  EXPECT_EQ(static_cast<uint8_t>(Action::CycleFontFamily), 39);
  EXPECT_EQ(static_cast<uint8_t>(Action::ToggleHomeButton), 40);
  EXPECT_TRUE(readerquickactions::isSlotAction(static_cast<uint8_t>(Action::ToggleHomeButton)));
  slots[0] = static_cast<uint8_t>(Action::CycleFontFamily);
  JsonDocument cycleDoc;
  write(cycleDoc, slots);
  EXPECT_FALSE(read(cycleDoc.as<JsonVariantConst>(), restored));
  EXPECT_EQ(restored, slots);
  slots[0] = static_cast<uint8_t>(Action::ToggleHomeButton);
  JsonDocument homeDoc;
  write(homeDoc, slots);
  EXPECT_FALSE(read(homeDoc.as<JsonVariantConst>(), restored));
  EXPECT_EQ(restored, slots);
}

#include "personalization/LegacyHomeShortcut.h"
#include "personalization/ReaderFontCycle.h"

TEST(ReaderQuickActions, LegacyHomePowerMigrationRespectsExplicitModernBinding) {
  JsonDocument legacy;
  legacy["shortPwrBtn"] = 26;
  uint8_t action = 0;
  EXPECT_TRUE(readershortcuts::migrateLegacyHomePowerTap(legacy.as<JsonVariantConst>(), true, action));
  EXPECT_EQ(action, static_cast<uint8_t>(readershortcuts::Action::ToggleHomeButton));
  action = 0;
  EXPECT_FALSE(readershortcuts::migrateLegacyHomePowerTap(legacy.as<JsonVariantConst>(), false, action));
  EXPECT_EQ(action, 0);
  legacy["powerTapAction"] = 0;
  EXPECT_FALSE(readershortcuts::migrateLegacyHomePowerTap(legacy.as<JsonVariantConst>(), true, action));
  legacy.remove("powerTapAction");
  legacy["shortPwrBtn"] = 25;
  EXPECT_FALSE(readershortcuts::migrateLegacyHomePowerTap(legacy.as<JsonVariantConst>(), true, action));
  legacy["shortPwrBtn"] = "26";
  EXPECT_FALSE(readershortcuts::migrateLegacyHomePowerTap(legacy.as<JsonVariantConst>(), true, action));
  EXPECT_EQ(action, 0);
}
TEST(ReaderFontCycle, CyclesInstalledFamiliesWithoutTruncatingNames) {
  struct Family {
    std::string name;
  };
  const std::vector<Family> families{{""}, {"Longer than the setting"}, {"Serif"}, {"Sans"}};
  uint8_t builtin = 0;
  char name[8] = "";
  EXPECT_TRUE(readerfontcycle::advance(builtin, 1, name, sizeof(name), families));
  EXPECT_STREQ(name, "Serif");
  EXPECT_TRUE(readerfontcycle::advance(builtin, 1, name, sizeof(name), families));
  EXPECT_STREQ(name, "Sans");
  EXPECT_TRUE(readerfontcycle::advance(builtin, 1, name, sizeof(name), families));
  EXPECT_STREQ(name, "");
  const std::vector<Family> none;
  EXPECT_FALSE(readerfontcycle::advance(builtin, 1, name, sizeof(name), none));
  std::strcpy(name, "Missing");
  EXPECT_FALSE(readerfontcycle::advance(builtin, 1, name, sizeof(name), families));
  EXPECT_STREQ(name, "Missing");
  name[0] = '\0';
  const std::vector<Family> unicode{{"長い名前"}, {"字"}};
  EXPECT_TRUE(readerfontcycle::advance(builtin, 1, name, sizeof(name), unicode));
  EXPECT_STREQ(name, "字");
  EXPECT_TRUE(readerfontcycle::advance(builtin, 1, name, sizeof(name), unicode));
  EXPECT_STREQ(name, "");
  char invalid[2] = {'x', 'y'};
  EXPECT_FALSE(readerfontcycle::advance(builtin, 1, invalid, sizeof(invalid), unicode));
  EXPECT_EQ(invalid[0], 'x');
  EXPECT_EQ(invalid[1], 'y');
}
