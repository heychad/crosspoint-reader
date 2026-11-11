#include <gtest/gtest.h>

#include "personalization/CrossInkControlImport.h"

namespace {
using A = readershortcuts::Action;
using H = HomeButtonAction;
struct ActionCase {
  uint8_t raw;
  A action;
};
// CrossInk v2.0.0-rc b8feaec4d: SHORT_PWRBTN and POWER_CHORD_ACTION.
// Guide dots are Later, Nearby is Skip, and Quick Actions has separate ownership.
constexpr ActionCase POWER[] = {{0, A::Disabled},           {1, A::Sleep},
                                {2, A::NextPage},           {3, A::Refresh},
                                {4, A::CycleFontFamily},    {6, A::ToggleFocus},
                                {7, A::Bookmark},           {8, A::SyncProgress},
                                {9, A::ToggleCompletion},   {10, A::ReadingStatistics},
                                {11, A::Screenshot},        {12, A::CycleAutoTurn},
                                {13, A::FileTransfer},      {14, A::ToggleTilt},
                                {15, A::ToggleNight},       {16, A::Footnotes},
                                {17, A::FileBrowser},       {18, A::CalibreWireless},
                                {19, A::JoinNetwork},       {20, A::CreateHotspot},
                                {21, A::CreateClipping},    {22, A::Dictionary},
                                {26, A::ToggleHomeButton},  {28, A::ToggleFrontlight},
                                {29, A::ToggleTouch},       {30, A::QuickLock},
                                {31, A::PreviousPage},      {33, A::Library},
                                {34, A::SleepOnly},         {35, A::WakeOnly},
                                {36, A::HomeReader},        {37, A::Home},
                                {38, A::SelectChapter},     {39, A::RotateHalfTurn},
                                {40, A::TogglePageGestures}};
constexpr ActionCase CHORD[] = {
    {0, A::Screenshot},      {1, A::QuickLock},        {4, A::Disabled},          {5, A::Sleep},
    {6, A::NextPage},        {7, A::Bookmark},         {8, A::ReadingStatistics}, {9, A::ToggleCompletion},
    {10, A::Refresh},        {11, A::CycleFontFamily}, {13, A::ToggleFocus},      {14, A::CycleAutoTurn},
    {15, A::SyncProgress},   {16, A::FileTransfer},    {17, A::CalibreWireless},  {18, A::JoinNetwork},
    {19, A::CreateHotspot},  {20, A::ToggleNight},     {21, A::Footnotes},        {22, A::FileBrowser},
    {23, A::CreateClipping}, {24, A::Dictionary},      {25, A::ToggleHomeButton}, {27, A::ToggleFrontlight},
    {28, A::ToggleTouch},    {29, A::PreviousPage},    {31, A::Library},          {32, A::HomeReader},
    {33, A::Home},           {34, A::SelectChapter},   {35, A::RotateHalfTurn},   {36, A::TogglePageGestures}};
constexpr ActionCase SIDE_EXTRA[] = {{64, A::PreviousChapter}, {65, A::NextChapter},    {66, A::LargerText},
                                     {67, A::SmallerText},     {68, A::RotateBackward}, {69, A::Rotate},
                                     {70, A::RotateHalfTurn}};
template <typename T, size_t N>
const T* find(const T (&cases)[N], uint8_t raw) {
  for (const auto& entry : cases)
    if (entry.raw == raw) return &entry;
  return nullptr;
}
}  // namespace

TEST(RcShortcutCatalog, PowerSideAndChordCatalogsMatchPinnedReleaseCandidate) {
  for (unsigned value = 0; value <= UINT8_MAX; ++value) {
    const auto raw = static_cast<uint8_t>(value);
    SCOPED_TRACE(value);
    auto action = A::Count;
    const auto* power = find(POWER, raw);
    EXPECT_EQ(readershortcuts::readLegacyPowerAction(raw, action), power != nullptr);
    EXPECT_EQ(action, power ? power->action : A::Count);
    action = A::Count;
    const auto* side = raw == 34 || raw == 35 ? nullptr : power;
    if (!side) side = find(SIDE_EXTRA, raw);
    EXPECT_EQ(readershortcuts::readLegacySideAction(raw, false, action), side != nullptr);
    EXPECT_EQ(action, side ? side->action : A::Count);
    action = A::Count;
    const auto* chord = find(CHORD, raw);
    EXPECT_EQ(readershortcuts::readLegacySideAction(raw, true, action), chord != nullptr);
    EXPECT_EQ(action, chord ? chord->action : A::Count);
  }
}

TEST(RcShortcutCatalog, HomeCatalogRetainsItsSeparateMeaningAndRejectsUnknownValues) {
  struct HomeCase {
    uint8_t raw;
    H action;
  };
  constexpr HomeCase cases[] = {{0, H::Ignore},
                                {1, H::Sleep},
                                {2, H::NextPage},
                                {3, H::Refresh},
                                {4, H::CycleFontFamily},
                                {6, H::ToggleFocus},
                                {7, H::Bookmark},
                                {8, H::Sync},
                                {9, H::ToggleCompletion},
                                {10, H::ReadingStatistics},
                                {11, H::Screenshot},
                                {12, H::CycleAutoTurn},
                                {13, H::FileTransfer},
                                {14, H::ToggleTilt},
                                {15, H::ToggleNight},
                                {16, H::Footnotes},
                                {17, H::FileBrowser},
                                {18, H::CalibreWireless},
                                {19, H::JoinNetwork},
                                {20, H::CreateHotspot},
                                {21, H::CreateClipping},
                                {22, H::Dictionary},
                                {23, H::Home},
                                {24, H::ToggleFrontlight},
                                {25, H::ReaderMenu},
                                {26, H::ToggleHomeButton},
                                {28, H::ToggleFrontlight},
                                {29, H::ToggleTouch},
                                {30, H::QuickLock},
                                {31, H::PreviousPage},
                                {33, H::Library},
                                {34, H::Sleep},
                                {36, H::HomeReader},
                                {37, H::Home},
                                {38, H::SelectChapter},
                                {39, H::RotateHalfTurn},
                                {40, H::TogglePageGestures}};
  for (unsigned value = 0; value <= UINT8_MAX; ++value) {
    SCOPED_TRACE(value);
    auto action = H::Count;
    const auto* expected = find(cases, static_cast<uint8_t>(value));
    EXPECT_EQ(readershortcuts::readLegacyHomeAction(value, action), expected != nullptr);
    EXPECT_EQ(action, expected ? expected->action : H::Count);
  }
}

TEST(RcShortcutCatalog, QuickSlotsPreserveAllOrNothingImportAcrossTheFullByteRange) {
  for (unsigned value = 0; value <= UINT8_MAX; ++value) {
    SCOPED_TRACE(value);
    const auto* expected = find(POWER, static_cast<uint8_t>(value));
    const bool supported = expected && value != 30 && value != 34 && value != 35;
    JsonDocument source;
    auto input = source["quickActionSlots"].to<JsonArray>();
    for (unsigned slot = 0; slot < 5; ++slot) input.add(value);
    readerquickactions::Slots slots{7, 7, 7, 7, 7};
    EXPECT_EQ(readershortcuts::readEmbeddedLegacyQuickSlots(source, slots), supported);
    const auto wanted = supported ? (value == 0 ? 0 : static_cast<uint8_t>(expected->action)) : 7;
    for (auto slot : slots) EXPECT_EQ(slot, wanted);
  }
}

TEST(RcShortcutCatalog, RcFrontButtonOwnersCannotHideAQuickActionsConflict) {
  for (const char* key : {"frontButtonLeftLong", "frontButtonRightLong"}) {
    JsonDocument source, target;
    source["shortPwrBtn"] = 27;
    source["quickActionsTrigger"] = 1;
    ASSERT_TRUE(readershortcuts::uniqueLegacyQuickOwner(source, 1));
    source[key] = 27;
    EXPECT_FALSE(readershortcuts::uniqueLegacyQuickOwner(source, 1));
    target["powerTapAction"] = static_cast<uint8_t>(A::Bookmark);
    EXPECT_FALSE(crossinkimport::stageControls(source, target, true, false));
    EXPECT_EQ(target["powerTapAction"].as<uint8_t>(), static_cast<uint8_t>(A::Bookmark));
    source[key] = "invalid";
    EXPECT_FALSE(readershortcuts::uniqueLegacyQuickOwner(source, 1));
  }
  for (unsigned trigger : {14, 15}) {
    JsonDocument source, target;
    target.to<JsonObject>();
    source["quickActionsTrigger"] = trigger;
    source[trigger == 14 ? "frontButtonLeftLong" : "frontButtonRightLong"] = 27;
    EXPECT_TRUE(readershortcuts::uniqueLegacyQuickOwner(source, trigger));
    EXPECT_FALSE(crossinkimport::stageControls(source, target, true, false));
    EXPECT_TRUE(target.as<JsonObject>().size() == 0);
  }
}
