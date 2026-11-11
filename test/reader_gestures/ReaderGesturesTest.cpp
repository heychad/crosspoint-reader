#include <gtest/gtest.h>

#include "personalization/ReaderFontStep.h"
#include "personalization/ReaderGestureJson.h"
#include "personalization/ReaderGestures.h"
using namespace readergestures;
namespace {
Frame frame(uint8_t count = 0) {
  Frame f;
  f.width = 480;
  f.height = 800;
  f.count = f.reportedCount = count;
  f.multiSupported = true;
  f.idsStable = true;
  f.now = 100;
  f.points = {{{10, 400, 1, true}, {200, 400, 2, true}}};
  return f;
}
Config enabled() {
  Config c;
  c.enabled = true;
  return c;
}
void ready(Controller& c) { c.update(frame(), enabled()); }
}  // namespace

TEST(ReaderGestureMigration, ImportsEmbeddedLegacyActionsIntoEachEdge) {
  constexpr std::array<Action, 13> expected = {Action::Default,
                                               Action::Brighter,
                                               Action::Dimmer,
                                               Action::Warmer,
                                               Action::Cooler,
                                               Action::NextChapter,
                                               Action::PreviousChapter,
                                               Action::LargerText,
                                               Action::SmallerText,
                                               Action::Home,
                                               Action::HomeReader,
                                               Action::SelectChapter,
                                               Action::TogglePageGestures};
  for (uint8_t value = 0; value < expected.size(); ++value) {
    JsonDocument doc;
    doc["leftEdgeUp"] = value;
    doc["leftEdgeDown"] = value;
    doc["rightEdgeUp"] = value;
    doc["rightEdgeDown"] = value;
    std::array<uint8_t, 4> fields{99, 99, 99, 99};
    EXPECT_TRUE(readEmbeddedLegacyEdges(doc, {&fields[0], &fields[1], &fields[2], &fields[3]}));
    for (auto field : fields) EXPECT_EQ(field, static_cast<uint8_t>(expected[value]));
  }
}

TEST(ReaderGestureMigration, EdgePaletteRetainsIdsAndExcludesPhysicalOnlyActions) {
  ASSERT_EQ(readershortcuts::EDGE_ACTIONS.size(), 37);
  for (unsigned raw = 0; raw <= 255; ++raw) {
    const bool supported = raw < 34 || raw == 42 || raw == 43 || raw == 44;
    EXPECT_EQ(readershortcuts::isEdgeAction(static_cast<uint8_t>(raw)), supported);
    auto normalized = static_cast<uint8_t>(raw);
    EXPECT_EQ(readershortcuts::normalizeEdgeAction(normalized), !supported);
    EXPECT_EQ(normalized, supported ? raw : 0);
    if (supported) {
      const auto index = readershortcuts::edgeActionIndex(static_cast<uint8_t>(raw));
      ASSERT_LT(index, readershortcuts::EDGE_ACTIONS.size());
      EXPECT_EQ(static_cast<uint8_t>(readershortcuts::EDGE_ACTIONS[index]), raw);
    }
  }
}

TEST(ReaderGestureMigration, ExistingModernKeysTakePrecedenceEvenWhenNullOrInvalid) {
  JsonDocument doc;
  doc["leftEdgeUp"] = 1;
  doc["leftEdgeDown"] = 2;
  doc["rightEdgeUp"] = 3;
  doc["rightEdgeDown"] = 4;
  doc["readerLeftEdgeUp"] = 0;
  doc["readerLeftEdgeDown"] = nullptr;
  doc["readerRightEdgeUp"] = "invalid";
  std::array<uint8_t, 4> fields{10, 11, 12, 13};
  EXPECT_TRUE(readEmbeddedLegacyEdges(doc, {&fields[0], &fields[1], &fields[2], &fields[3]}));
  EXPECT_EQ(fields[0], 10);
  EXPECT_EQ(fields[1], 11);
  EXPECT_EQ(fields[2], 12);
  EXPECT_EQ(fields[3], static_cast<uint8_t>(actionFor(4)));
}

TEST(ReaderGestureMigration, MissingAndMalformedLegacyValuesDoNotChangeDestinations) {
  JsonDocument doc;
  doc["leftEdgeUp"] = -1;
  doc["leftEdgeDown"] = 256;
  doc["rightEdgeUp"] = "1";
  doc["rightEdgeDown"] = static_cast<uint8_t>(Binding::Count);
  std::array<uint8_t, 4> fields{10, 11, 12, 13};
  EXPECT_FALSE(readEmbeddedLegacyEdges(doc, {&fields[0], &fields[1], &fields[2], &fields[3]}));
  EXPECT_EQ(fields, (std::array<uint8_t, 4>{10, 11, 12, 13}));
}

TEST(ReaderGestureMigration, BooleanAndFractionalValuesAreNotBindings) {
  JsonDocument doc;
  doc["leftEdgeUp"] = true;
  doc["leftEdgeDown"] = 1.5;
  std::array<uint8_t, 4> fields{10, 11, 12, 13};
  EXPECT_FALSE(readEmbeddedLegacyEdges(doc, {&fields[0], &fields[1], &fields[2], &fields[3]}));
  EXPECT_EQ(fields, (std::array<uint8_t, 4>{10, 11, 12, 13}));
}
TEST(ReaderGestures, InheritedContactNeverBecomesAGesture) {
  Controller c;
  auto cfg = enabled();
  cfg.edges[0] = Action::Brighter;
  auto f = frame(1);
  EXPECT_TRUE(c.update(f, cfg).consumed);
  f.points[0].y = 200;
  EXPECT_EQ(c.update(f, cfg).count, 0);
  c.update(frame(), cfg);
  c.update(frame(1), cfg);
  EXPECT_EQ(c.update(f, cfg).events[0].kind, EventKind::LightBegin);
}
TEST(ReaderGestures, NormalTapAndUnassignedEdgeRemainAvailable) {
  Controller c;
  ready(c);
  auto cfg = enabled();
  EXPECT_FALSE(c.update(frame(1), cfg).consumed);
  auto f = frame(1);
  f.points[0].y = 200;
  EXPECT_FALSE(c.update(f, cfg).consumed);
  EXPECT_FALSE(c.update(frame(), cfg).consumed);
}
TEST(ReaderGestures, LiveEdgeLightFollowsFingerAndFinishesOnce) {
  Controller c;
  ready(c);
  auto cfg = enabled();
  cfg.edges[0] = Action::Brighter;
  c.update(frame(1), cfg);
  auto f = frame(1);
  f.points[0].y = 200;
  auto result = c.update(f, cfg);
  ASSERT_EQ(result.count, 1);
  EXPECT_TRUE(result.started);
  EXPECT_TRUE(result.suppressContact);
  EXPECT_EQ(result.events[0].kind, EventKind::LightBegin);
  EXPECT_GT(result.events[0].amount, 0);
  const int peak = result.events[0].amount;
  f.points[0].y = 300;
  result = c.update(f, cfg);
  EXPECT_LT(result.events[0].amount, peak);
  result = c.update(frame(), cfg);
  EXPECT_EQ(result.events[result.count - 1].kind, EventKind::LightFinish);
  EXPECT_EQ(c.update(frame(), cfg).count, 0);
}
TEST(ReaderGestures, DriftingOffEdgePreventsDiscreteActionOnReturn) {
  Controller c;
  ready(c);
  auto cfg = enabled();
  cfg.edges[0] = Action::NextChapter;
  c.update(frame(1), cfg);
  auto f = frame(1);
  f.points[0].y = 200;
  EXPECT_TRUE(c.update(f, cfg).started);
  f.points[0].x = 120;
  EXPECT_TRUE(c.update(f, cfg).suppressContact);
  f.points[0].x = 10;
  EXPECT_EQ(c.update(f, cfg).count, 0);
  EXPECT_EQ(c.update(frame(), cfg).count, 0);
}
TEST(ReaderGestures, FastCompletedEdgeSwipeUsesFinalCoordinate) {
  Controller c;
  ready(c);
  auto cfg = enabled();
  cfg.edges[0] = Action::NextChapter;
  c.update(frame(1), cfg);
  auto f = frame();
  f.singleSwipe = true;
  f.endX = 10;
  f.endY = 200;
  const auto result = c.update(f, cfg);
  ASSERT_EQ(result.count, 1);
  EXPECT_EQ(result.events[0].action, Action::NextChapter);
}
TEST(ReaderGestures, MultiContactStartsOwnershipAndEndsWithoutSingleFingerLeak) {
  Controller c;
  ready(c);
  auto cfg = enabled();
  EXPECT_TRUE(c.update(frame(2), cfg).started);
  EXPECT_TRUE(c.update(frame(1), cfg).consumed);
  EXPECT_TRUE(c.update(frame(), cfg).consumed);
  EXPECT_FALSE(c.update(frame(), cfg).consumed);
}
TEST(ReaderGestures, PinchAppliesOneStepAndSuppressesCompletedDuplicate) {
  Controller c;
  ready(c);
  auto cfg = enabled();
  auto f = frame(2);
  f.points[0].x = 100;
  f.points[1].x = 300;
  c.update(f, cfg);
  f.points[0].x = 75;
  f.points[1].x = 325;
  EXPECT_EQ(c.update(f, cfg).count, 0);
  const auto result = c.update(f, cfg);
  ASSERT_EQ(result.count, 1);
  EXPECT_EQ(result.events[0].action, Action::LargerText);
  EXPECT_TRUE(result.suppressContact);
  auto end = frame();
  end.pinch = true;
  end.pinchScale = 1.25;
  EXPECT_EQ(c.update(end, cfg).count, 0);
}
TEST(ReaderGestures, CompletedRotationOverridesAndRollsBackLiveLight) {
  Controller c;
  ready(c);
  auto cfg = enabled();
  cfg.multi[0] = static_cast<uint8_t>(Binding::Brighter);
  auto f = frame(2);
  c.update(f, cfg);
  f.points[0].y -= 200;
  f.points[1].y -= 200;
  auto result = c.update(f, cfg);
  ASSERT_EQ(result.count, 1);
  EXPECT_EQ(result.events[0].kind, EventKind::LightBegin);
  auto end = frame();
  end.rotation = true;
  end.rotationDegrees = -40;
  result = c.update(end, cfg);
  ASSERT_EQ(result.count, 2);
  EXPECT_EQ(result.events[0].kind, EventKind::LightCancel);
  EXPECT_EQ(result.events[1].action, Action::Rotate);
}
TEST(ReaderGestures, DisabledPinchAndRotationNeverEmitLayoutActions) {
  Controller c;
  ready(c);
  auto cfg = enabled();
  cfg.pinch = cfg.rotation = false;
  c.update(frame(2), cfg);
  auto f = frame();
  f.pinch = f.rotation = true;
  f.pinchScale = 1.5;
  f.rotationDegrees = 40;
  EXPECT_EQ(c.update(f, cfg).count, 0);
}
TEST(ReaderGestures, ContactIdReplacementCancelsLiveLight) {
  Controller c;
  ready(c);
  auto cfg = enabled();
  cfg.multi[0] = static_cast<uint8_t>(Binding::Brighter);
  auto f = frame(2);
  c.update(f, cfg);
  f.points[0].y -= 200;
  f.points[1].y -= 200;
  c.update(f, cfg);
  f.points[0].id = 9;
  auto result = c.update(f, cfg);
  ASSERT_EQ(result.count, 1);
  EXPECT_EQ(result.events[0].kind, EventKind::LightCancel);
  EXPECT_EQ(c.update(frame(), cfg).count, 0);
}
TEST(ReaderGestures, LayoutOrOverlayChangeFinishesLiveLightAndBlocksRemainder) {
  for (bool overlay : {false, true}) {
    Controller c;
    ready(c);
    auto cfg = enabled();
    cfg.edges[0] = Action::Brighter;
    c.update(frame(1), cfg);
    auto f = frame(1);
    f.points[0].y = 200;
    c.update(f, cfg);
    if (overlay)
      cfg.enabled = false;
    else
      f.orientation = 1;
    auto result = c.update(f, cfg);
    ASSERT_EQ(result.count, 1);
    EXPECT_EQ(result.events[0].kind, EventKind::LightFinish);
    EXPECT_TRUE(result.suppressContact);
  }
}
TEST(ReaderGestures, InvalidContactCountIsNeverAPageTap) {
  Controller c;
  ready(c);
  auto cfg = enabled();
  auto f = frame(2);
  f.reportedCount = 3;
  EXPECT_TRUE(c.update(f, cfg).consumed);
  EXPECT_EQ(c.update(f, cfg).count, 0);
}
TEST(ReaderGestureBindings, EachActionHasOneDirectionAndLatestEditWins) {
  std::array<uint8_t, 4> bindings{1, 1, 1, 1};
  normalize(bindings, 2);
  EXPECT_EQ(bindings, (std::array<uint8_t, 4>{0, 0, 1, 0}));
  bindings = {2, 2, 3, 255};
  normalize(bindings);
  EXPECT_EQ(bindings, (std::array<uint8_t, 4>{2, 0, 3, 0}));
}
TEST(ReaderGestureBindings, JsonPreservesDefaultsAndRejectsInvalidValues) {
  JsonDocument doc;
  deserializeJson(doc,
                  R"({"twoFingerSwipeUp":1,"twoFingerSwipeDown":1,"twoFingerSwipeLeft":-2,"twoFingerSwipeRight":"3"})");
  std::array<uint8_t, 4> values{};
  EXPECT_TRUE(readBindings(doc.as<JsonVariantConst>(), values));
  EXPECT_EQ(values, (std::array<uint8_t, 4>{1, 0, 0, 0}));
  JsonDocument saved;
  writeBindings(saved, values);
  std::array<uint8_t, 4> restored{};
  EXPECT_FALSE(readBindings(saved.as<JsonVariantConst>(), restored));
  EXPECT_EQ(restored, values);
}
TEST(ReaderFontStep, SparseUnsortedDuplicateSizesStepWithoutWrapping) {
  const uint8_t sizes[]{18, 12, 0, 16, 12};
  uint8_t result = 99;
  auto step = [&](uint8_t current, bool larger) {
    return readerfonts::adjacentSize(std::size(sizes), [&](size_t i) { return sizes[i]; }, current, larger, result);
  };
  EXPECT_TRUE(step(12, true));
  EXPECT_EQ(result, 16);
  EXPECT_TRUE(step(17, false));
  EXPECT_EQ(result, 12);
  EXPECT_FALSE(step(18, true));
  EXPECT_FALSE(step(12, false));
}

TEST(ReaderGestures, InheritedContactReleaseCannotLeakATap) {
  Controller c;
  auto cfg = enabled();
  EXPECT_TRUE(c.update(frame(1), cfg).suppressContact);
  EXPECT_TRUE(c.update(frame(), cfg).consumed);
  EXPECT_FALSE(c.update(frame(), cfg).consumed);
}
TEST(ReaderGestures, ReplacedSingleFingerRollsBackLightAndConsumesRelease) {
  Controller c;
  ready(c);
  auto cfg = enabled();
  cfg.edges[0] = Action::Brighter;
  c.update(frame(1), cfg);
  auto f = frame(1);
  f.points[0].y = 200;
  c.update(f, cfg);
  f.points[0].id = 9;
  const auto result = c.update(f, cfg);
  ASSERT_EQ(result.count, 1);
  EXPECT_EQ(result.events[0].kind, EventKind::LightCancel);
  EXPECT_TRUE(result.suppressContact);
  EXPECT_TRUE(c.update(frame(), cfg).consumed);
}

TEST(ReaderGestures, DuplicateStableContactIdsCannotStartPinch) {
  Controller c;
  ready(c);
  auto cfg = enabled();
  auto f = frame(2);
  f.points[1].id = f.points[0].id;
  auto result = c.update(f, cfg);
  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(result.suppressContact);
  f.points[1].x = 350;
  EXPECT_EQ(c.update(f, cfg).count, 0);
}

TEST(ReaderGestures, DisabledContactsStayWithOverlayThroughRelease) {
  Controller c;
  ready(c);
  Config cfg;
  for (uint8_t count : {1, 1, 0}) {
    const auto result = c.update(frame(count), cfg);
    EXPECT_FALSE(result.consumed);
    EXPECT_FALSE(result.suppressContact);
    EXPECT_EQ(result.count, 0);
  }
}
TEST(ReaderGestures, ReenablingDuringOverlayContactDoesNotCreatePageGesture) {
  Controller c;
  ready(c);
  Config off;
  c.update(frame(1), off);
  auto cfg = enabled();
  cfg.edges[0] = Action::Brighter;
  auto f = frame(1);
  f.points[0].y = 200;
  auto result = c.update(f, cfg);
  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(result.suppressContact);
  EXPECT_EQ(result.count, 0);
  EXPECT_TRUE(c.update(frame(), cfg).consumed);
  EXPECT_FALSE(c.update(frame(), cfg).consumed);
}
TEST(ReaderGestures, ReenablingOnReleaseConsumesInheritedRelease) {
  Controller c;
  ready(c);
  Config off;
  c.update(frame(1), off);
  const auto result = c.update(frame(), enabled());
  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(result.suppressContact);
}
TEST(ReaderGestures, BusyRendererPassesUnownedContactToNormalInput) {
  Controller c;
  ready(c);
  EXPECT_FALSE(c.suspend(true, false).consumed);
  auto cfg = enabled();
  cfg.edges[0] = Action::Brighter;
  auto f = frame(1);
  f.points[0].y = 100;
  EXPECT_FALSE(c.update(f, cfg).consumed);
  EXPECT_FALSE(c.update(frame(), cfg).consumed);
}
TEST(ReaderGestures, BusyRendererRollsBackOwnedLightAndConsumesRemainder) {
  Controller c;
  ready(c);
  auto cfg = enabled();
  cfg.edges[0] = Action::Brighter;
  c.update(frame(1), cfg);
  auto f = frame(1);
  f.points[0].y = 100;
  c.update(f, cfg);
  const auto result = c.suspend(true, false);
  ASSERT_EQ(result.count, 1);
  EXPECT_EQ(result.events[0].kind, EventKind::LightCancel);
  EXPECT_TRUE(result.consumed);
  EXPECT_TRUE(result.suppressContact);
  EXPECT_TRUE(c.update(frame(), cfg).consumed);
}
TEST(ReaderGestures, ShrinkingToReplacementContactRollsBackMultiLight) {
  Controller c;
  ready(c);
  auto cfg = enabled();
  cfg.multi[0] = static_cast<uint8_t>(Binding::Brighter);
  auto f = frame(2);
  c.update(f, cfg);
  f.points[0].y -= 200;
  f.points[1].y -= 200;
  c.update(f, cfg);
  f.count = f.reportedCount = 1;
  f.points[0].id = 9;
  auto result = c.update(f, cfg);
  ASSERT_EQ(result.count, 1);
  EXPECT_EQ(result.events[0].kind, EventKind::LightCancel);
  EXPECT_TRUE(result.suppressContact);
  EXPECT_TRUE(c.update(frame(), cfg).consumed);
}
TEST(ReaderGestures, ShrinkingToOriginalContactFinishesMultiLight) {
  Controller c;
  ready(c);
  auto cfg = enabled();
  cfg.multi[0] = static_cast<uint8_t>(Binding::Brighter);
  auto f = frame(2);
  c.update(f, cfg);
  f.points[0].y -= 200;
  f.points[1].y -= 200;
  c.update(f, cfg);
  f.count = f.reportedCount = 1;
  const auto result = c.update(f, cfg);
  ASSERT_EQ(result.count, 1);
  EXPECT_EQ(result.events[0].kind, EventKind::LightFinish);
}

TEST(ReaderGestures, InitialRenderContentionPreservesInheritedContactGuard) {
  Controller c;
  const auto suspended = c.suspend(true, false);
  EXPECT_TRUE(suspended.consumed);
  EXPECT_TRUE(suspended.suppressContact);
  EXPECT_TRUE(c.update(frame(1), enabled()).consumed);
  EXPECT_TRUE(c.update(frame(), enabled()).consumed);
  EXPECT_FALSE(c.update(frame(), enabled()).consumed);
}

TEST(ReaderGestureBindings, ChapterAndHomeActionsKeepCrossInkIds) {
  EXPECT_EQ(static_cast<uint8_t>(Binding::Home), 9);
  EXPECT_EQ(static_cast<uint8_t>(Binding::HomeReader), 10);
  EXPECT_EQ(static_cast<uint8_t>(Binding::SelectChapter), 11);
  EXPECT_EQ(actionFor(11), Action::SelectChapter);
  JsonDocument doc;
  doc["twoFingerSwipeUp"] = 11;
  std::array<uint8_t, 4> bindings{};
  EXPECT_FALSE(readBindings(doc, bindings));
  EXPECT_EQ(bindings[0], 11);
  doc["leftEdgeUp"] = 11;
  std::array<uint8_t, 4> edges{0, 0, 0, 0};
  EXPECT_TRUE(readEmbeddedLegacyEdges(doc, {&edges[0], &edges[1], &edges[2], &edges[3]}));
  EXPECT_EQ(edges[0], static_cast<uint8_t>(Action::SelectChapter));
}
