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

#include "personalization/CrossInkBarImport.h"
#include "personalization/CrossInkControlImport.h"
#include "personalization/LegacyHomeShortcut.h"
#include "personalization/LegacyPowerShortcuts.h"
#include "personalization/LegacySideShortcuts.h"
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
  legacy["powerTapAction"] = nullptr;
  EXPECT_FALSE(readershortcuts::migrateLegacyHomePowerTap(legacy.as<JsonVariantConst>(), true, action));
  legacy.remove("powerTapAction");
  legacy["shortPwrBtn"] = 25;
  EXPECT_FALSE(readershortcuts::migrateLegacyHomePowerTap(legacy.as<JsonVariantConst>(), true, action));
  legacy["shortPwrBtn"] = "26";
  EXPECT_FALSE(readershortcuts::migrateLegacyHomePowerTap(legacy.as<JsonVariantConst>(), true, action));
  EXPECT_EQ(action, 0);
}

TEST(ReaderShortcuts, LegacySideMigrationRespectsCurrentValuesAndEnumFamilies) {
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, R"({"sideButtonOrientationAware":0,"sideButtonUpShort":7,
      "sideButtonUpLong":64,"sideButtonDownShort":16,"sideButtonDownLong":27,
      "sideButtonChordAction":7,"readerDownTap":null})"));
  std::array<uint8_t, 5> values{};
  EXPECT_TRUE(readershortcuts::readEmbeddedLegacySides(doc.as<JsonVariantConst>(),
                                                       {&values[0], &values[1], &values[2], &values[3], &values[4]}));
  using A = readershortcuts::Action;
  EXPECT_EQ(values[0], static_cast<uint8_t>(A::Bookmark));
  EXPECT_EQ(values[1], static_cast<uint8_t>(A::PreviousChapter));
  EXPECT_EQ(values[2], 0);  // Explicit null remains owned by the modern key.
  EXPECT_EQ(values[3], 0);  // Quick Actions needs trigger ownership.
  EXPECT_EQ(values[4], static_cast<uint8_t>(A::Bookmark));
  doc["sideButtonChordAction"] = 0;
  EXPECT_TRUE(readershortcuts::readEmbeddedLegacySides(doc.as<JsonVariantConst>(),
                                                       {&values[0], &values[1], &values[2], &values[3], &values[4]}));
  EXPECT_EQ(values[4], static_cast<uint8_t>(A::Screenshot));
}

TEST(ReaderShortcuts, LegacySideMigrationRejectsUnknownOrientationAndInvalidActions) {
  JsonDocument doc;
  doc["sideButtonUpShort"] = 2;
  std::array<uint8_t, 5> values{};
  const auto apply = [&] {
    return readershortcuts::readEmbeddedLegacySides(doc.as<JsonVariantConst>(),
                                                    {&values[0], &values[1], &values[2], &values[3], &values[4]});
  };
  EXPECT_FALSE(apply());
  doc["sideButtonOrientationAware"] = 2;
  EXPECT_FALSE(apply());
  doc["sideButtonOrientationAware"] = 0;
  for (const auto* invalid : {"\"2\"", "-1", "255", "true", "null"}) {
    JsonDocument input;
    ASSERT_FALSE(deserializeJson(input, invalid));
    doc["sideButtonUpShort"] = input.as<JsonVariantConst>();
    EXPECT_FALSE(apply()) << invalid;
  }
  EXPECT_EQ(values[0], 0);
}

TEST(ReaderShortcuts, LegacySideMigrationUsesIndependentSideAndChordCatalogs) {
  using A = readershortcuts::Action;
  struct Case {
    uint8_t raw;
    A expected;
  };
  constexpr Case singles[] = {{0, A::Disabled},          {1, A::Sleep},
                              {2, A::NextPage},          {3, A::Refresh},
                              {4, A::CycleFontFamily},   {6, A::ToggleFocus},
                              {7, A::Bookmark},          {8, A::SyncProgress},
                              {9, A::ToggleCompletion},  {10, A::ReadingStatistics},
                              {11, A::Screenshot},       {13, A::FileTransfer},
                              {14, A::ToggleTilt},       {15, A::ToggleNight},
                              {16, A::Footnotes},        {17, A::FileBrowser},
                              {21, A::CreateClipping},   {22, A::Dictionary},
                              {26, A::ToggleHomeButton}, {28, A::ToggleFrontlight},
                              {29, A::ToggleTouch},      {30, A::QuickLock},
                              {31, A::PreviousPage},     {33, A::Library},
                              {36, A::HomeReader},       {38, A::SelectChapter},
                              {12, A::CycleAutoTurn},    {37, A::Home},
                              {39, A::RotateHalfTurn},   {64, A::PreviousChapter},
                              {65, A::NextChapter},      {66, A::LargerText},
                              {67, A::SmallerText},      {68, A::RotateBackward},
                              {69, A::Rotate},           {70, A::RotateHalfTurn}};
  constexpr Case chords[] = {{0, A::Screenshot},        {1, A::QuickLock},
                             {4, A::Disabled},          {5, A::Sleep},
                             {6, A::NextPage},          {7, A::Bookmark},
                             {8, A::ReadingStatistics}, {9, A::ToggleCompletion},
                             {10, A::Refresh},          {11, A::CycleFontFamily},
                             {13, A::ToggleFocus},      {15, A::SyncProgress},
                             {16, A::FileTransfer},     {20, A::ToggleNight},
                             {21, A::Footnotes},        {22, A::FileBrowser},
                             {23, A::CreateClipping},   {24, A::Dictionary},
                             {25, A::ToggleHomeButton}, {27, A::ToggleFrontlight},
                             {28, A::ToggleTouch},      {29, A::PreviousPage},
                             {31, A::Library},          {32, A::HomeReader},
                             {14, A::CycleAutoTurn},    {33, A::Home},
                             {34, A::SelectChapter},    {35, A::RotateHalfTurn}};
  for (const auto& value : singles) {
    A actual = A::Default;
    ASSERT_TRUE(readershortcuts::readLegacySideAction(value.raw, false, actual));
    EXPECT_EQ(actual, value.expected) << static_cast<unsigned>(value.raw);
  }
  for (const auto& value : chords) {
    A actual = A::Default;
    ASSERT_TRUE(readershortcuts::readLegacySideAction(value.raw, true, actual));
    EXPECT_EQ(actual, value.expected) << static_cast<unsigned>(value.raw);
  }
  A unchanged = A::Default;
  EXPECT_FALSE(readershortcuts::readLegacySideAction(27, false, unchanged));
  EXPECT_FALSE(readershortcuts::readLegacySideAction(26, true, unchanged));
  EXPECT_EQ(unchanged, A::Default);
}

TEST(ReaderShortcuts, LegacySideOrientationRespectsExplicitNativeSetting) {
  JsonDocument doc;
  doc["sideButtonOrientationAware"] = 1;
  doc["sideButtonUpShort"] = 7;
  uint8_t orientation = 0;
  ASSERT_TRUE(readershortcuts::readEmbeddedLegacySideOrientation(doc.as<JsonVariantConst>(), orientation));
  EXPECT_EQ(orientation, 1);
  std::array<uint8_t, 5> sides{};
  const auto apply = [&] {
    return readershortcuts::readEmbeddedLegacySides(doc.as<JsonVariantConst>(),
                                                    {&sides[0], &sides[1], &sides[2], &sides[3], &sides[4]});
  };
  ASSERT_TRUE(apply());
  EXPECT_EQ(sides[0], static_cast<uint8_t>(readershortcuts::Action::Bookmark));
  sides[0] = 0;
  doc["readerDownTap"] = 2;
  EXPECT_FALSE(readershortcuts::readEmbeddedLegacySideOrientation(doc.as<JsonVariantConst>(), orientation));
  EXPECT_FALSE(apply());
  doc.remove("readerDownTap");
  doc["readerSideFollowOrientation"] = 0;
  EXPECT_FALSE(readershortcuts::readEmbeddedLegacySideOrientation(doc.as<JsonVariantConst>(), orientation));
  EXPECT_FALSE(apply());
  EXPECT_EQ(sides[0], 0);
  doc["readerSideFollowOrientation"] = 1;
  EXPECT_TRUE(apply());
  doc.remove("readerUpTap");
  doc["sideButtonUpShort"] = 27;
  doc["quickActionsTrigger"] = 10;
  EXPECT_TRUE(readershortcuts::readEmbeddedLegacySideQuickAction(
      doc.as<JsonVariantConst>(), {&sides[0], &sides[1], &sides[2], &sides[3], &sides[4]}, false));
  EXPECT_EQ(sides[0], static_cast<uint8_t>(readershortcuts::Action::QuickActions));
}

TEST(ReaderShortcuts, LegacyPowerUsesDistinctCatalogsAndPreservesModernBindings) {
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, R"({"longPwrBtn":7,"powerChordAction":0})"));
  std::array<uint8_t, 2> values{};
  const auto apply = [&] {
    return readershortcuts::readEmbeddedLegacyPower(doc.as<JsonVariantConst>(), {&values[0], &values[1]});
  };
  ASSERT_TRUE(apply());
  EXPECT_EQ(values[0], static_cast<uint8_t>(readershortcuts::Action::Bookmark));
  EXPECT_EQ(values[1], static_cast<uint8_t>(readershortcuts::Action::Screenshot));
  values = {};
  doc["powerHoldAction"] = nullptr;
  doc["powerUpChordAction"] = 0;
  EXPECT_FALSE(apply());
  EXPECT_EQ(values[0], 0);
  EXPECT_EQ(values[1], 0);
  doc.remove("powerHoldAction");
  doc.remove("powerUpChordAction");
  doc["longPwrBtn"] = 27;
  doc["powerChordAction"] = 26;
  EXPECT_FALSE(apply());  // Quick Actions requires ownership checks.
  doc["longPwrBtn"] = 64;
  doc["powerChordAction"] = 2;
  EXPECT_FALSE(apply());
  doc["longPwrBtn"] = 34;
  ASSERT_TRUE(apply());
  EXPECT_EQ(values[0], static_cast<uint8_t>(readershortcuts::Action::Sleep));
}

TEST(ReaderShortcuts, ImportsTaggedRcPowerActionsWithoutOverwritingNativeSettings) {
  const std::array<readershortcuts::Action, 4> expected = {
      readershortcuts::Action::Home, readershortcuts::Action::SelectChapter, readershortcuts::Action::RotateHalfTurn,
      readershortcuts::Action::TogglePageGestures};
  for (size_t i = 0; i < expected.size(); ++i) {
    JsonDocument doc;
    doc["shortPwrBtn"] = 37 + i;
    doc["longPwrBtn"] = 37 + i;
    readershortcuts::Action tap = readershortcuts::Action::Default;
    ASSERT_TRUE(readershortcuts::readLegacyPowerTap(doc.as<JsonVariantConst>(), tap));
    EXPECT_EQ(tap, expected[i]);
    std::array<uint8_t, 2> values{};
    ASSERT_TRUE(readershortcuts::readEmbeddedLegacyPower(doc.as<JsonVariantConst>(), {&values[0], &values[1]}));
    EXPECT_EQ(values[0], static_cast<uint8_t>(expected[i]));
    EXPECT_EQ(values[1], 0);
    doc["powerHoldAction"] = nullptr;
    values[0] = static_cast<uint8_t>(readershortcuts::Action::Bookmark);
    EXPECT_FALSE(readershortcuts::readEmbeddedLegacyPower(doc.as<JsonVariantConst>(), {&values[0], &values[1]}));
    EXPECT_EQ(values[0], static_cast<uint8_t>(readershortcuts::Action::Bookmark));
  }
}

TEST(ReaderShortcuts, RejectsPowerValuesOutsideTaggedCatalogWithoutChangingBindings) {
  for (unsigned raw = 41; raw <= 255; ++raw) {
    JsonDocument doc;
    doc["shortPwrBtn"] = raw;
    doc["longPwrBtn"] = raw;
    readershortcuts::Action tap = readershortcuts::Action::Bookmark;
    EXPECT_FALSE(readershortcuts::readLegacyPowerTap(doc.as<JsonVariantConst>(), tap));
    EXPECT_EQ(tap, readershortcuts::Action::Bookmark);
    std::array<uint8_t, 2> values = {static_cast<uint8_t>(readershortcuts::Action::Bookmark), 0};
    EXPECT_FALSE(readershortcuts::readEmbeddedLegacyPower(doc.as<JsonVariantConst>(), {&values[0], &values[1]}));
    EXPECT_EQ(values[0], static_cast<uint8_t>(readershortcuts::Action::Bookmark));
  }
}

TEST(ReaderShortcuts, LegacyQuickActionsImportOnlyOneUnclaimedPowerTrigger) {
  constexpr const char* legacy[] = {"shortPwrBtn", "longPwrBtn", "powerChordAction"};
  constexpr const char* current[] = {"powerTapAction", "powerHoldAction", "powerUpChordAction"};
  constexpr uint8_t triggers[] = {1, 2, 5};
  for (size_t i = 0; i < 3; ++i) {
    JsonDocument doc;
    doc["quickActionsTrigger"] = triggers[i];
    doc[legacy[i]] = i == 2 ? 26 : 27;
    std::array<uint8_t, 3> values{};
    const auto apply = [&](bool owned) {
      return readershortcuts::readEmbeddedLegacyPowerQuickAction(doc.as<JsonVariantConst>(),
                                                                 {&values[0], &values[1], &values[2]}, owned);
    };
    EXPECT_FALSE(apply(true));
    ASSERT_TRUE(apply(false));
    EXPECT_EQ(values[i], static_cast<uint8_t>(readershortcuts::Action::QuickActions));
    values[i] = 0;
    doc[current[i]] = nullptr;
    EXPECT_FALSE(apply(false));
    doc.remove(current[i]);
    doc["sideButtonUpShort"] = 27;
    EXPECT_FALSE(apply(false));
    EXPECT_EQ(values[i], 0);
  }
}

TEST(CrossInkControlImport, QuickActionsCanUsePowerOrHomeWithoutReplacingExistingOwner) {
  constexpr const char* legacy[] = {"shortPwrBtn",
                                    "longPwrBtn",
                                    "powerChordAction",
                                    "homeButtonTapAction",
                                    "homeButtonLongPressAction",
                                    "homeButtonDoubleTapAction"};
  constexpr const char* current[] = {"powerTapAction",      "powerHoldAction",           "powerUpChordAction",
                                     "homeButtonTapAction", "homeButtonLongPressAction", "homeButtonDoubleTapAction"};
  constexpr uint8_t triggers[] = {1, 2, 5, 6, 7, 8};
  for (size_t i = 0; i < 6; ++i) {
    JsonDocument source, target;
    source["quickActionsTrigger"] = triggers[i];
    source[legacy[i]] = i == 2 ? 26 : 27;
    source["homeButtonInReaderEnabled"] = 1;
    target["homeButtonInReaderEnabled"] = 1;
    for (const auto* name : current) target[name] = 0;
    ASSERT_TRUE(crossinkimport::stageControls(source.as<JsonVariantConst>(), target, true, false)) << i;
    const auto expected = i < 3 ? static_cast<uint8_t>(readershortcuts::Action::QuickActions)
                                : static_cast<uint8_t>(HomeButtonAction::QuickActions);
    EXPECT_EQ(target[current[i]].as<uint8_t>(), expected) << i;
    target[current[i]] = 0;
    EXPECT_FALSE(crossinkimport::stageControls(source.as<JsonVariantConst>(), target, true, true)) << i;
    EXPECT_EQ(target[current[i]].as<uint8_t>(), 0);
  }
}

TEST(CrossInkControlImport, ImportsAdditionalHomeReaderActionsByMeaning) {
  struct Mapping {
    uint8_t legacy;
    HomeButtonAction action;
  };
  for (const auto mapping : {Mapping{1, HomeButtonAction::Sleep},
                             {4, HomeButtonAction::CycleFontFamily},
                             {9, HomeButtonAction::ToggleCompletion},
                             {11, HomeButtonAction::Screenshot},
                             {14, HomeButtonAction::ToggleTilt},
                             {15, HomeButtonAction::ToggleNight},
                             {28, HomeButtonAction::ToggleFrontlight},
                             {29, HomeButtonAction::ToggleTouch},
                             {31, HomeButtonAction::PreviousPage},
                             {34, HomeButtonAction::Sleep}}) {
    JsonDocument source, target;
    source["homeButtonInReaderEnabled"] = 1;
    source["homeButtonTapAction"] = mapping.legacy;
    target["homeButtonInReaderEnabled"] = 1;
    target["homeButtonTapAction"] = static_cast<uint8_t>(HomeButtonAction::Ignore);
    ASSERT_TRUE(crossinkimport::stageControls(source.as<JsonVariantConst>(), target, true, false));
    EXPECT_EQ(target["homeButtonTapAction"].as<uint8_t>(), static_cast<uint8_t>(mapping.action));
  }
}

TEST(CrossInkControlImport, PreservesDisabledHomeUnlessReplacementRequested) {
  JsonDocument source, target;
  ASSERT_FALSE(deserializeJson(source, R"({"homeButtonInReaderEnabled":1,"homeButtonTapAction":23,
      "homeButtonLongPressAction":7,"homeButtonDoubleTapAction":24})"));
  ASSERT_FALSE(deserializeJson(target, R"({"homeButtonInReaderEnabled":0,"homeButtonTapAction":1,
      "homeButtonLongPressAction":1,"homeButtonDoubleTapAction":1})"));
  EXPECT_FALSE(crossinkimport::stageControls(source.as<JsonVariantConst>(), target, false, false));
  EXPECT_EQ(target["homeButtonInReaderEnabled"].as<uint8_t>(), 0);
  ASSERT_TRUE(crossinkimport::stageControls(source.as<JsonVariantConst>(), target, true, false));
  EXPECT_EQ(target["homeButtonInReaderEnabled"].as<uint8_t>(), 1);
  EXPECT_EQ(target["homeButtonTapAction"].as<uint8_t>(), static_cast<uint8_t>(HomeButtonAction::Home));
  EXPECT_EQ(target["homeButtonLongPressAction"].as<uint8_t>(), static_cast<uint8_t>(HomeButtonAction::Bookmark));
  EXPECT_EQ(target["homeButtonDoubleTapAction"].as<uint8_t>(),
            static_cast<uint8_t>(HomeButtonAction::ToggleFrontlight));
}

TEST(CrossInkControlImport, ImportsHomeActionsWhenNativeDefaultsAreImplicit) {
  JsonDocument source, target;
  ASSERT_FALSE(deserializeJson(source, R"({"homeButtonInReaderEnabled":1,"homeButtonTapAction":28,
      "homeButtonDoubleTapAction":30,"homeButtonLongPressAction":7})"));
  target.to<JsonObject>();
  ASSERT_TRUE(crossinkimport::stageControls(source.as<JsonVariantConst>(), target, false, false));
  EXPECT_EQ(target["homeButtonTapAction"].as<uint8_t>(), static_cast<uint8_t>(HomeButtonAction::ToggleFrontlight));
  EXPECT_EQ(target["homeButtonDoubleTapAction"].as<uint8_t>(), static_cast<uint8_t>(HomeButtonAction::QuickLock));
  EXPECT_EQ(target["homeButtonLongPressAction"].as<uint8_t>(), static_cast<uint8_t>(HomeButtonAction::Bookmark));
  EXPECT_EQ(target["homeButtonInReaderEnabled"].as<uint8_t>(), 1);

  JsonDocument nullEnabled;
  ASSERT_FALSE(deserializeJson(nullEnabled, R"({"homeButtonInReaderEnabled":null})"));
  EXPECT_FALSE(crossinkimport::stageControls(source.as<JsonVariantConst>(), nullEnabled, false, false));
  EXPECT_TRUE(nullEnabled["homeButtonTapAction"].isUnbound());
  EXPECT_TRUE(crossinkimport::stageControls(source.as<JsonVariantConst>(), nullEnabled, true, false));
  EXPECT_EQ(nullEnabled["homeButtonInReaderEnabled"].as<uint8_t>(), 1);
}

TEST(CrossInkBarImport, MapsItemsByMeaningAndKeepsOtherSettings) {
  JsonDocument source, target;
  ASSERT_FALSE(deserializeJson(source, R"({"readerStatusBars":{"version":1,
    "top":{"slots":[1,2,3,8,9,10,0],"percentageFormat":1,"progressBar":0,"thickness":2},
    "bottom":{"slots":[5,7,0,8,2,1,10],"percentageFormat":2,"progressBar":1,"thickness":0}}})"));
  ASSERT_FALSE(deserializeJson(target, R"({"readerBarsEnabled":false,"unrelated":42})"));
  ASSERT_EQ(crossinkimport::stageBars(source.as<JsonVariantConst>(), target), crossinkimport::BarResult::Imported);
  ReaderBar top, bottom;
  ASSERT_TRUE(readReaderBar(target["readerBarsV1"]["top"], top));
  ASSERT_TRUE(readReaderBar(target["readerBarsV1"]["bottom"], bottom));
  EXPECT_EQ(top.slots[0], ReaderBarItem::Clock);
  EXPECT_EQ(top.slots[2], ReaderBarItem::TimeLeftBook);
  EXPECT_EQ(top.slots[3], ReaderBarItem::BookTitle);
  EXPECT_EQ(bottom.slots[0], ReaderBarItem::ChapterPages);
  EXPECT_EQ(bottom.slots[1], ReaderBarItem::BookPercent);
  EXPECT_EQ(top.progress, 0);
  EXPECT_EQ(bottom.progress, 1);
  EXPECT_EQ(top.percentageDecimals, 1);
  EXPECT_EQ(bottom.thickness, 0);
  EXPECT_TRUE(target["readerBarsEnabled"].as<bool>());
  EXPECT_EQ(target["unrelated"].as<int>(), 42);
  EXPECT_EQ(crossinkimport::stageBars(source.as<JsonVariantConst>(), target), crossinkimport::BarResult::Unchanged);
}

TEST(CrossInkBarImport, RefusesUnsupportedOrDamagedLayoutsWithoutChanges) {
  JsonDocument source, target;
  ASSERT_FALSE(deserializeJson(source, R"({"readerStatusBars":{"version":1,
    "top":{"slots":[0,0,0,8,0,0,0],"percentageFormat":0,"progressBar":2,"thickness":1},
    "bottom":{"slots":[0,0,0,0,0,0,0],"percentageFormat":0,"progressBar":2,"thickness":1}}})"));
  ASSERT_FALSE(deserializeJson(target, R"({"readerBarsEnabled":false,"unrelated":42})"));
  for (int unsupported : {4, 6}) {
    source["readerStatusBars"]["top"]["slots"][0] = unsupported;
    EXPECT_EQ(crossinkimport::stageBars(source.as<JsonVariantConst>(), target), crossinkimport::BarResult::Unsupported);
    EXPECT_TRUE(target["readerBarsV1"].isUnbound());
  }
  source["readerStatusBars"]["top"]["slots"][0] = 0;
  source["readerStatusBars"]["bottom"]["progressBar"] = 3;
  EXPECT_EQ(crossinkimport::stageBars(source.as<JsonVariantConst>(), target), crossinkimport::BarResult::Invalid);
  EXPECT_TRUE(target["readerBarsV1"].isUnbound());
  EXPECT_EQ(target["unrelated"].as<int>(), 42);
}

TEST(CrossInkBarImport, SixSlotLayoutKeepsCenterAligned) {
  JsonDocument source, target;
  ASSERT_FALSE(deserializeJson(source, R"({"readerStatusBars":{"version":1,
    "top":{"slots":[1,2,8,9,10,0],"percentageFormat":0,"progressBar":2,"thickness":1},
    "bottom":{"slots":[0,0,0,0,0,0],"percentageFormat":0,"progressBar":2,"thickness":1}}})"));
  target.to<JsonObject>();
  ASSERT_EQ(crossinkimport::stageBars(source.as<JsonVariantConst>(), target), crossinkimport::BarResult::Imported);
  ReaderBar top;
  ASSERT_TRUE(readReaderBar(target["readerBarsV1"]["top"], top));
  EXPECT_EQ(top.slots[2], ReaderBarItem::Empty);
  EXPECT_EQ(top.slots[3], ReaderBarItem::BookTitle);
  EXPECT_EQ(top.slots[4], ReaderBarItem::ChapterTitle);
}

TEST(CrossInkControlImport, PowerImportCannotDisplaceExistingQuickTrigger) {
  JsonDocument source, target;
  ASSERT_FALSE(deserializeJson(source, R"({"longPwrBtn":7,"powerChordAction":0})"));
  ASSERT_FALSE(deserializeJson(target, R"({"powerHoldAction":21,"powerUpChordAction":0})"));
  ASSERT_TRUE(crossinkimport::stageControls(source.as<JsonVariantConst>(), target, true, true));
  EXPECT_EQ(target["powerHoldAction"].as<int>(), 21);
  EXPECT_EQ(target["powerUpChordAction"].as<int>(), static_cast<int>(readershortcuts::Action::Screenshot));
}

TEST(CrossInkControlImport, ShortPowerUsesLegacyMeaningOnlyForSelectedSource) {
  JsonDocument source, target;
  ASSERT_FALSE(deserializeJson(source, R"({"shortPwrBtn":7,"longPwrBtn":1,"powerChordAction":0})"));
  ASSERT_FALSE(deserializeJson(target, R"({"shortPwrBtn":4,"powerTapAction":0})"));
  ASSERT_TRUE(crossinkimport::stageControls(source.as<JsonVariantConst>(), target, true, false));
  EXPECT_EQ(target["shortPwrBtn"].as<int>(), 4);
  EXPECT_EQ(target["powerTapAction"].as<int>(), static_cast<int>(readershortcuts::Action::Bookmark));
  EXPECT_EQ(target["powerHoldAction"].as<int>(), static_cast<int>(readershortcuts::Action::Sleep));
  EXPECT_EQ(target["powerUpChordAction"].as<int>(), static_cast<int>(readershortcuts::Action::Screenshot));
  source["shortPwrBtn"] = 4;
  target["powerTapAction"] = static_cast<uint8_t>(readershortcuts::Action::Bookmark);
  EXPECT_TRUE(crossinkimport::stageControls(source.as<JsonVariantConst>(), target, true, false));
  EXPECT_EQ(target["powerTapAction"].as<int>(), static_cast<int>(readershortcuts::Action::CycleFontFamily));
}

TEST(CrossInkControlImport, StagesLegacySideOrientationWithoutDisplacingQuickOwner) {
  JsonDocument source, target;
  ASSERT_FALSE(deserializeJson(source, R"({"sideButtonOrientationAware":1,"sideButtonUpShort":7})"));
  ASSERT_FALSE(deserializeJson(target, R"({"readerSideFollowOrientation":0,"readerUpTap":2})"));
  EXPECT_FALSE(crossinkimport::stageControls(source.as<JsonVariantConst>(), target, false, false));
  EXPECT_FALSE(crossinkimport::stageControls(source.as<JsonVariantConst>(), target, true, true));
  ASSERT_TRUE(crossinkimport::stageControls(source.as<JsonVariantConst>(), target, true, false));
  EXPECT_EQ(target["readerSideFollowOrientation"].as<int>(), 1);
  EXPECT_EQ(target["readerUpTap"].as<int>(), static_cast<int>(readershortcuts::Action::Bookmark));
}

TEST(ReaderShortcuts, LegacySideQuickActionRequiresOneMatchingUnclaimedTrigger) {
  constexpr const char* names[] = {"sideButtonUpShort", "sideButtonUpLong", "sideButtonDownShort", "sideButtonDownLong",
                                   "sideButtonChordAction"};
  constexpr const char* modern[] = {"readerUpTap", "readerUpHold", "readerDownTap", "readerDownHold",
                                    "readerSideChord"};
  for (size_t i = 0; i < 5; ++i) {
    JsonDocument doc;
    doc["sideButtonOrientationAware"] = 0;
    doc["quickActionsTrigger"] = i == 4 ? 9 : static_cast<unsigned>(i + 10);
    doc[names[i]] = i == 4 ? 26 : 27;
    std::array<uint8_t, 5> values{};
    const auto apply = [&](bool owned) {
      return readershortcuts::readEmbeddedLegacySideQuickAction(
          doc.as<JsonVariantConst>(), {&values[0], &values[1], &values[2], &values[3], &values[4]}, owned);
    };
    EXPECT_FALSE(apply(true));
    EXPECT_TRUE(apply(false));
    EXPECT_EQ(values[i], static_cast<uint8_t>(readershortcuts::Action::QuickActions));
    values[i] = 0;
    doc[modern[i]] = nullptr;
    EXPECT_FALSE(apply(false));
    doc.remove(modern[i]);
    doc["shortPwrBtn"] = 27;
    EXPECT_FALSE(apply(false));
    doc.remove("shortPwrBtn");
    doc[names[(i + 1) % 5]] = (i + 1) % 5 == 4 ? 26 : 27;
    EXPECT_FALSE(apply(false));
    EXPECT_EQ(values[i], 0);
  }
}

TEST(ReaderShortcuts, LegacySideQuickActionRejectsMissingAndConflictingData) {
  JsonDocument doc;
  doc["sideButtonOrientationAware"] = 0;
  doc["quickActionsTrigger"] = 10;
  doc["sideButtonUpShort"] = 27;
  std::array<uint8_t, 5> values{};
  const auto apply = [&] {
    return readershortcuts::readEmbeddedLegacySideQuickAction(
        doc.as<JsonVariantConst>(), {&values[0], &values[1], &values[2], &values[3], &values[4]}, false);
  };
  doc["sideButtonOrientationAware"] = 2;
  EXPECT_FALSE(apply());
  doc["sideButtonOrientationAware"] = 0;
  doc["quickActionsTrigger"] = 11;
  EXPECT_FALSE(apply());
  doc["quickActionsTrigger"] = 10;
  doc["powerChordAction"] = 26;
  EXPECT_FALSE(apply());
  doc["powerChordAction"] = "26";
  EXPECT_FALSE(apply());
  doc.remove("powerChordAction");
  EXPECT_TRUE(apply());
}

TEST(ReaderShortcuts, LegacyQuickSlotsImportAtomicallyAndRespectNativeSlots) {
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, R"({"quickActionSlots":[7,8,11,15,22]})"));
  readerquickactions::Slots slots{1, 2, 3, 4, 5};
  using A = readershortcuts::Action;
  ASSERT_TRUE(readershortcuts::readEmbeddedLegacyQuickSlots(doc.as<JsonVariantConst>(), slots));
  const readerquickactions::Slots expected{static_cast<uint8_t>(A::Bookmark), static_cast<uint8_t>(A::SyncProgress),
                                           static_cast<uint8_t>(A::Screenshot), static_cast<uint8_t>(A::ToggleNight),
                                           static_cast<uint8_t>(A::Dictionary)};
  EXPECT_EQ(slots, expected);
  doc["readerQuickActionsV1"] = nullptr;
  doc["quickActionSlots"][0] = 0;
  EXPECT_FALSE(readershortcuts::readEmbeddedLegacyQuickSlots(doc.as<JsonVariantConst>(), slots));
  EXPECT_EQ(slots, expected);
  doc.remove("readerQuickActionsV1");
  for (const auto* invalid : {"[7,8,11]", "[7,8,11,15,30]", "[7,8,11,15,64]", "[7,8,11,15,\"22\"]", "[7,8,11,15,5]"}) {
    JsonDocument source;
    ASSERT_FALSE(deserializeJson(source, invalid));
    doc["quickActionSlots"] = source.as<JsonVariantConst>();
    EXPECT_FALSE(readershortcuts::readEmbeddedLegacyQuickSlots(doc.as<JsonVariantConst>(), slots)) << invalid;
    EXPECT_EQ(slots, expected);
  }
  ASSERT_FALSE(deserializeJson(doc, R"({"quickActionSlots":[0,0,0,0,0]})"));
  ASSERT_TRUE(readershortcuts::readEmbeddedLegacyQuickSlots(doc.as<JsonVariantConst>(), slots));
  EXPECT_EQ(slots, (readerquickactions::Slots{}));
  ASSERT_FALSE(deserializeJson(doc, R"({"quickActionSlots":[14,26,0,0,0]})"));
  ASSERT_TRUE(readershortcuts::readEmbeddedLegacyQuickSlots(doc.as<JsonVariantConst>(), slots));
  EXPECT_EQ(slots[0], static_cast<uint8_t>(A::ToggleTilt));
  EXPECT_EQ(slots[1], static_cast<uint8_t>(A::ToggleHomeButton));
  ASSERT_FALSE(deserializeJson(doc, R"({"quickActionSlots":[6,0,0,0,0]})"));
  ASSERT_TRUE(readershortcuts::readEmbeddedLegacyQuickSlots(doc.as<JsonVariantConst>(), slots));
  EXPECT_EQ(slots[0], static_cast<uint8_t>(A::ToggleFocus));
  ASSERT_FALSE(deserializeJson(doc, R"({"quickActionSlots":[9,0,0,0,0]})"));
  ASSERT_TRUE(readershortcuts::readEmbeddedLegacyQuickSlots(doc.as<JsonVariantConst>(), slots));
  EXPECT_EQ(slots[0], static_cast<uint8_t>(A::ToggleCompletion));
}

TEST(CrossInkControlImport, StagesSeparateSourceWithoutTouchingOtherPreferences) {
  JsonDocument source, current;
  ASSERT_FALSE(deserializeJson(source, R"({"sideButtonOrientationAware":0,"sideButtonUpShort":7,
      "sideButtonDownShort":27,"quickActionsTrigger":12,"quickActionSlots":[7,8,11,15,22]})"));
  ASSERT_FALSE(deserializeJson(current, R"({"readerUpTap":2,"pluginsEnabled":1,"other":{"x":3}})"));
  ASSERT_TRUE(crossinkimport::stageControls(source.as<JsonVariantConst>(), current, false, false));
  EXPECT_EQ(current["readerUpTap"].as<int>(), 2);
  EXPECT_EQ(current["readerDownTap"].as<uint8_t>(), static_cast<uint8_t>(readershortcuts::Action::QuickActions));
  EXPECT_EQ(current["readerQuickActionsV1"].as<JsonArrayConst>().size(), 5);
  EXPECT_EQ(current["pluginsEnabled"].as<int>(), 1);
  EXPECT_EQ(current["other"]["x"].as<int>(), 3);
  EXPECT_EQ(source["sideButtonUpShort"].as<int>(), 7);
  ASSERT_TRUE(crossinkimport::stageControls(source.as<JsonVariantConst>(), current, true, true));
  EXPECT_EQ(current["readerUpTap"].as<uint8_t>(), static_cast<uint8_t>(readershortcuts::Action::Bookmark));
  EXPECT_EQ(current["readerDownTap"].as<uint8_t>(), static_cast<uint8_t>(readershortcuts::Action::QuickActions));
}

TEST(CrossInkControlImport, ImportsFontCycleForButtonsAndQuickActions) {
  JsonDocument source, current;
  ASSERT_FALSE(deserializeJson(source, R"({"sideButtonOrientationAware":0,"sideButtonUpShort":4,
      "sideButtonChordAction":11,"shortPwrBtn":4,"quickActionSlots":[4,0,0,0,0]})"));
  current.to<JsonObject>();
  ASSERT_TRUE(crossinkimport::stageControls(source.as<JsonVariantConst>(), current, false, false));
  const auto expected = static_cast<uint8_t>(readershortcuts::Action::CycleFontFamily);
  EXPECT_EQ(current["readerUpTap"].as<uint8_t>(), expected);
  EXPECT_EQ(current["readerSideChord"].as<uint8_t>(), expected);
  EXPECT_EQ(current["powerTapAction"].as<uint8_t>(), expected);
  EXPECT_EQ(current["readerQuickActionsV1"][0].as<uint8_t>(), expected);
}

TEST(CrossInkControlImport, RetainsQuickOwnerAndRejectsInvalidLegacySlots) {
  JsonDocument source, current;
  ASSERT_FALSE(deserializeJson(source, R"({"sideButtonOrientationAware":0,"sideButtonUpShort":2,
      "sideButtonDownShort":27,"quickActionsTrigger":12,"quickActionSlots":[7,8,11,15,5]})"));
  ASSERT_FALSE(deserializeJson(current, R"({"readerUpTap":21,"readerQuickActionsV1":[7,8,9,10,11]})"));
  EXPECT_FALSE(crossinkimport::stageControls(source.as<JsonVariantConst>(), current, true, true));
  EXPECT_EQ(current["readerUpTap"].as<int>(), 21);
  EXPECT_EQ(current["readerQuickActionsV1"][0].as<int>(), 7);
  EXPECT_TRUE(current["readerDownTap"].isUnbound());
  source["sideButtonOrientationAware"] = 1;
  EXPECT_FALSE(crossinkimport::stageControls(source.as<JsonVariantConst>(), current, true, true));
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

TEST(CrossInkControlImport, ImportsCompatibleTouchGesturesFromSeparateSource) {
  JsonDocument source, current;
  ASSERT_FALSE(deserializeJson(source, R"({"pinchFontResizeEnabled":0,"twoFingerRotationEnabled":1,
    "twoFingerSwipeUp":1,"twoFingerSwipeDown":2,"twoFingerSwipeLeft":5,"twoFingerSwipeRight":6,
    "leftEdgeUp":1,"leftEdgeDown":2,"rightEdgeUp":5,"rightEdgeDown":6})"));
  ASSERT_FALSE(deserializeJson(current, R"({"pinchFontResizeEnabled":1,"twoFingerRotationEnabled":0,
    "twoFingerSwipeUp":0,"twoFingerSwipeDown":0,"twoFingerSwipeLeft":0,"twoFingerSwipeRight":0,
    "readerLeftEdgeUp":0,"readerLeftEdgeDown":0,"readerRightEdgeUp":0,"readerRightEdgeDown":0,"pluginsEnabled":1})"));
  ASSERT_TRUE(crossinkimport::stageControls(source.as<JsonVariantConst>(), current, true, false));
  EXPECT_EQ(current["pinchFontResizeEnabled"].as<int>(), 0);
  EXPECT_EQ(current["twoFingerRotationEnabled"].as<int>(), 1);
  EXPECT_EQ(current["twoFingerSwipeUp"].as<int>(), 1);
  EXPECT_EQ(current["twoFingerSwipeDown"].as<int>(), 2);
  EXPECT_EQ(current["twoFingerSwipeLeft"].as<int>(), 5);
  EXPECT_EQ(current["twoFingerSwipeRight"].as<int>(), 6);
  EXPECT_EQ(current["readerLeftEdgeUp"].as<uint8_t>(), static_cast<uint8_t>(readershortcuts::Action::Brighter));
  EXPECT_EQ(current["readerLeftEdgeDown"].as<uint8_t>(), static_cast<uint8_t>(readershortcuts::Action::Dimmer));
  EXPECT_EQ(current["readerRightEdgeUp"].as<uint8_t>(), static_cast<uint8_t>(readershortcuts::Action::NextChapter));
  EXPECT_EQ(current["readerRightEdgeDown"].as<uint8_t>(),
            static_cast<uint8_t>(readershortcuts::Action::PreviousChapter));
  std::array<uint8_t, 4> loaded{};
  EXPECT_FALSE(readergestures::readBindings(current.as<JsonVariantConst>(), loaded));
  EXPECT_EQ((std::array<uint8_t, 4>{1, 2, 5, 6}), loaded);
  EXPECT_EQ(current["pluginsEnabled"].as<int>(), 1);
  EXPECT_EQ(source["leftEdgeUp"].as<int>(), 1);
}

TEST(CrossInkControlImport, PreservesQuickActionOwnerAndRejectsConflictingSwipes) {
  JsonDocument source, current;
  ASSERT_FALSE(deserializeJson(source, R"({"twoFingerSwipeUp":1,"twoFingerSwipeDown":1,"leftEdgeUp":2})"));
  ASSERT_FALSE(deserializeJson(current, R"({"twoFingerSwipeUp":3,"twoFingerSwipeDown":4,"readerLeftEdgeUp":21})"));
  EXPECT_FALSE(crossinkimport::stageGestureControls(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["twoFingerSwipeUp"].as<int>(), 3);
  EXPECT_EQ(current["twoFingerSwipeDown"].as<int>(), 4);
  EXPECT_EQ(current["readerLeftEdgeUp"].as<int>(), 21);
}

TEST(CrossInkControlImport, FillsOnlyCompatibleMissingTouchSettings) {
  JsonDocument source, current;
  ASSERT_FALSE(deserializeJson(source, R"({"pinchFontResizeEnabled":0,"twoFingerRotationEnabled":4,
    "twoFingerSwipeUp":1,"twoFingerSwipeDown":2,"leftEdgeUp":5,"rightEdgeDown":99})"));
  ASSERT_FALSE(deserializeJson(current, R"({"pinchFontResizeEnabled":null,"twoFingerSwipeUp":3,"other":42})"));
  ASSERT_TRUE(crossinkimport::stageGestureControls(source.as<JsonVariantConst>(), current, false));
  EXPECT_TRUE(current["pinchFontResizeEnabled"].isNull());
  EXPECT_TRUE(current["twoFingerRotationEnabled"].isUnbound());
  EXPECT_EQ(current["twoFingerSwipeUp"].as<int>(), 3);
  EXPECT_EQ(current["twoFingerSwipeDown"].as<int>(), 2);
  EXPECT_EQ(current["readerLeftEdgeUp"].as<uint8_t>(), static_cast<uint8_t>(readershortcuts::Action::NextChapter));
  EXPECT_TRUE(current["readerRightEdgeDown"].isUnbound());
  EXPECT_EQ(current["other"].as<int>(), 42);
}

TEST(CrossInkControlImport, ImportsEffectiveReaderTouchWithoutDisablingEdgeQuickActions) {
  JsonDocument source, current;
  ASSERT_FALSE(deserializeJson(source, R"({"disableReaderTouchscreen":1,"touchReaderControls":1})"));
  ASSERT_FALSE(deserializeJson(current, R"({"touchReaderControls":1})"));
  ASSERT_TRUE(crossinkimport::stageGestureControls(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["touchReaderControls"].as<int>(), 0);
  source["disableReaderTouchscreen"] = 0;
  ASSERT_TRUE(crossinkimport::stageGestureControls(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["touchReaderControls"].as<int>(), 1);
  source["touchReaderControls"] = 0;
  ASSERT_TRUE(crossinkimport::stageGestureControls(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["touchReaderControls"].as<int>(), 0);
  current["touchReaderControls"] = 1;
  current["readerLeftEdgeUp"] = static_cast<uint8_t>(readershortcuts::Action::QuickActions);
  EXPECT_FALSE(crossinkimport::stageGestureControls(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["touchReaderControls"].as<int>(), 1);
  source["disableReaderTouchscreen"] = 2;
  EXPECT_FALSE(crossinkimport::stageGestureControls(source.as<JsonVariantConst>(), current, true));
  source["disableReaderTouchscreen"] = 0;
  source.remove("touchReaderControls");
  EXPECT_FALSE(crossinkimport::stageGestureControls(source.as<JsonVariantConst>(), current, true));
}

TEST(CrossInkControlImport, ImportsPageTurnGesturesAndLegacyPreviousFallback) {
  JsonDocument source, current;
  ASSERT_FALSE(deserializeJson(source, R"({"pageTurnGesture":1,"previousPageGesture":3})"));
  ASSERT_FALSE(deserializeJson(current, R"({"pageTurnGesture":2,"previousPageGesture":4,"other":7})"));
  ASSERT_TRUE(crossinkimport::stageGestureControls(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["pageTurnGesture"].as<int>(), 1);
  EXPECT_EQ(current["previousPageGesture"].as<int>(), 3);
  source["pageTurnGesture"] = 2;
  source.remove("previousPageGesture");
  ASSERT_TRUE(crossinkimport::stageGestureControls(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["pageTurnGesture"].as<int>(), 2);
  EXPECT_EQ(current["previousPageGesture"].as<int>(), 2);
  source["pageTurnGesture"] = 3;
  source["previousPageGesture"] = nullptr;
  ASSERT_TRUE(crossinkimport::stageGestureControls(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["pageTurnGesture"].as<int>(), 3);
  EXPECT_EQ(current["previousPageGesture"].as<int>(), 3);
  source["pageTurnGesture"] = 4;
  EXPECT_FALSE(crossinkimport::stageGestureControls(source.as<JsonVariantConst>(), current, false));
  EXPECT_EQ(current["pageTurnGesture"].as<int>(), 3);
  source["pageTurnGesture"] = 9;
  EXPECT_FALSE(crossinkimport::stageGestureControls(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["previousPageGesture"].as<int>(), 3);
  EXPECT_EQ(current["other"].as<int>(), 7);
}

TEST(ReaderShortcuts, RcNetworkAndGestureActionsUseTheirOwnLegacyIds) {
  using A = readershortcuts::Action;
  for (const auto& entry : {std::pair{18, A::CalibreWireless}, std::pair{19, A::JoinNetwork},
                            std::pair{20, A::CreateHotspot}, std::pair{40, A::TogglePageGestures}}) {
    A mapped = A::Default;
    ASSERT_TRUE(readershortcuts::readLegacySideAction(entry.first, false, mapped));
    EXPECT_EQ(mapped, entry.second);
  }
  for (const auto& entry : {std::pair{17, A::CalibreWireless}, std::pair{18, A::JoinNetwork},
                            std::pair{19, A::CreateHotspot}, std::pair{36, A::TogglePageGestures}}) {
    A mapped = A::Default;
    ASSERT_TRUE(readershortcuts::readLegacySideAction(entry.first, true, mapped));
    EXPECT_EQ(mapped, entry.second);
  }
  JsonDocument legacy, current;
  legacy["disablePageTurnGestures"] = 1;
  EXPECT_TRUE(crossinkimport::stageGestureControls(legacy.as<JsonVariantConst>(), current, false));
  EXPECT_EQ(current["disablePageTurnGestures"].as<int>(), 1);
  legacy["disablePageTurnGestures"] = 0;
  EXPECT_FALSE(crossinkimport::stageGestureControls(legacy.as<JsonVariantConst>(), current, false));
  EXPECT_TRUE(crossinkimport::stageGestureControls(legacy.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["disablePageTurnGestures"].as<int>(), 0);
}
