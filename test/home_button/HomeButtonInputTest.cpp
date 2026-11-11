#include <gtest/gtest.h>

#include <cstdint>

#include "util/HomeButtonInput.h"

TEST(HomeButtonInput, RecognizesConfiguredGestures) {
  using A = HomeButtonAction;
  HomeButtonInput input;
  auto tick = [&](uint32_t now, bool tap = false, bool hold = false, bool swipe = false, bool press = false) {
    return input.update(now, tap, hold, swipe, press, A::Home, A::ToggleFrontlight, A::ReaderMenu);
  };
  EXPECT_EQ(tick(0, true), A::Ignore);
  EXPECT_EQ(tick(350), A::Ignore);
  EXPECT_EQ(tick(351), A::Home);
  EXPECT_EQ(tick(352), A::Ignore);
  EXPECT_EQ(tick(1000, true), A::Ignore);
  EXPECT_EQ(tick(1350, true), A::ToggleFrontlight);
  EXPECT_EQ(tick(1701), A::Ignore);
  EXPECT_EQ(tick(2000, false, true), A::ReaderMenu);
  EXPECT_EQ(tick(2001), A::Ignore);
  // Tap then hold: the second contact prevents an early single-tap action.
  EXPECT_EQ(tick(3000, true), A::Ignore);
  EXPECT_EQ(tick(3200, false, false, false, true), A::Ignore);
  EXPECT_EQ(tick(3400), A::Ignore);
  EXPECT_EQ(tick(3900, false, true), A::ReaderMenu);
  EXPECT_EQ(tick(4000), A::Ignore);
  // A bezel swipe cancels the Home-key event and any deferred action.
  EXPECT_EQ(tick(5000, true), A::Ignore);
  EXPECT_EQ(tick(5100, true, false, true), A::Ignore);
  EXPECT_EQ(tick(5500), A::Ignore);
  EXPECT_EQ(tick(6000, true), A::Ignore);
  input.reset();
  EXPECT_EQ(tick(6500), A::Ignore);
  // Unsigned subtraction remains valid over the millisecond clock wrap.
  EXPECT_EQ(tick(UINT32_MAX - 100, true), A::Ignore);
  EXPECT_EQ(tick(100, true), A::ToggleFrontlight);
  EXPECT_EQ(tick(UINT32_MAX - 100, true), A::Ignore);
  EXPECT_EQ(tick(251), A::Home);
  // Two separated taps each produce a single action.
  EXPECT_EQ(tick(7000, true), A::Ignore);
  EXPECT_EQ(tick(7400, true), A::Home);
  EXPECT_EQ(tick(7751), A::Home);
  // Disabling double tap removes the single-tap delay.
  EXPECT_EQ(input.update(8000, true, false, false, false, A::Bookmark, A::Ignore, A::ReaderMenu), A::Bookmark);
  // Every Home gesture can launch clipping selection without changing persisted action IDs.
  static_assert(static_cast<uint8_t>(A::ToggleFrontlight) == 10);
  EXPECT_EQ(input.update(9000, true, false, false, false, A::CreateClipping, A::Ignore, A::Home), A::CreateClipping);
  EXPECT_EQ(input.update(10000, true, false, false, false, A::Home, A::CreateClipping, A::Ignore), A::Ignore);
  EXPECT_EQ(input.update(10100, true, false, false, false, A::Home, A::CreateClipping, A::Ignore), A::CreateClipping);
  EXPECT_EQ(input.update(11000, false, true, false, false, A::Home, A::Ignore, A::CreateClipping), A::CreateClipping);
}

TEST(HomeButtonInput, NewReaderActionsKeepStableIdsAndGestures) {
  static_assert(static_cast<unsigned>(HomeButtonAction::QuickLock) == 13);
  static_assert(static_cast<unsigned>(HomeButtonAction::FileBrowser) == 14);
  static_assert(static_cast<unsigned>(HomeButtonAction::Library) == 15);
  static_assert(static_cast<unsigned>(HomeButtonAction::SavedItems) == 16);
  static_assert(static_cast<unsigned>(HomeButtonAction::FileTransfer) == 17);
  static_assert(static_cast<unsigned>(HomeButtonAction::ReadingStatistics) == 18);
  static_assert(static_cast<unsigned>(HomeButtonAction::CycleFontFamily) == 26);
  HomeButtonInput input;
  using A = HomeButtonAction;
  EXPECT_EQ(input.update(0, true, false, false, false, A::Library, A::SavedItems, A::FileTransfer), A::Ignore);
  EXPECT_EQ(input.update(100, false, false, false, true, A::Library, A::SavedItems, A::FileTransfer), A::Ignore);
  EXPECT_EQ(input.update(150, true, false, false, false, A::Library, A::SavedItems, A::FileTransfer), A::SavedItems);
  EXPECT_EQ(input.update(1000, false, true, false, false, A::Library, A::SavedItems, A::FileTransfer), A::FileTransfer);
  EXPECT_EQ(input.update(2000, true, false, false, false, A::ReadingStatistics, A::Ignore, A::FileBrowser),
            A::ReadingStatistics);
  EXPECT_EQ(input.update(3000, false, true, false, false, A::Home, A::Ignore, A::CycleFontFamily), A::CycleFontFamily);
}

TEST(HomeButtonInput, DiscardsHeldContactUntilItsReleaseThenAcceptsNewTap) {
  HomeButtonInput input;
  using A = HomeButtonAction;
  input.discardContact(true);
  EXPECT_EQ(input.update(100, false, true, false, false, A::Library, A::Ignore, A::FileTransfer, true), A::Ignore);
  EXPECT_EQ(input.update(200, true, false, false, false, A::Library, A::Ignore, A::FileTransfer, false), A::Ignore);
  EXPECT_EQ(input.update(300, false, false, false, true, A::Library, A::Ignore, A::FileTransfer, true), A::Ignore);
  EXPECT_EQ(input.update(400, true, false, false, false, A::Library, A::Ignore, A::FileTransfer, false), A::Library);
}

TEST(HomeButtonInput, DiscardClearsPendingTapAndAllowsFreshDoubleTap) {
  HomeButtonInput input;
  using A = HomeButtonAction;
  EXPECT_EQ(input.update(0, true, false, false, false, A::Library, A::SavedItems, A::FileTransfer), A::Ignore);
  input.discardContact(false);
  EXPECT_EQ(input.update(500, false, false, false, false, A::Library, A::SavedItems, A::FileTransfer), A::Ignore);
  EXPECT_EQ(input.update(600, true, false, false, false, A::Library, A::SavedItems, A::FileTransfer), A::Ignore);
  EXPECT_EQ(input.update(650, false, false, false, true, A::Library, A::SavedItems, A::FileTransfer), A::Ignore);
  EXPECT_EQ(input.update(700, true, false, false, false, A::Library, A::SavedItems, A::FileTransfer), A::SavedItems);
}

TEST(HomeButtonInput, DisabledReaderPageClearsPendingAndPreservesGestureBindings) {
  HomeButtonInput input;
  using A = HomeButtonAction;
  const auto tick = [&](uint32_t now, bool tapped, bool held, bool pressed, bool down, bool enabled) {
    return input.update(now, tapped, held, false, pressed, A::Library, A::SavedItems, A::ReaderMenu, down, enabled);
  };
  EXPECT_EQ(tick(0, true, false, false, false, true), A::Ignore);
  EXPECT_EQ(tick(100, false, false, false, false, false), A::Ignore);
  EXPECT_EQ(tick(500, false, false, false, false, true), A::Ignore);
  EXPECT_EQ(tick(600, false, false, true, true, false), A::Ignore);
  EXPECT_EQ(tick(700, false, true, false, true, false), A::Ignore);
  EXPECT_EQ(tick(800, true, false, false, false, true), A::Ignore);
  EXPECT_EQ(tick(900, true, false, false, false, true), A::Ignore);
  EXPECT_EQ(tick(1000, false, false, true, true, true), A::Ignore);
  EXPECT_EQ(tick(1100, true, false, false, false, true), A::SavedItems);
  EXPECT_EQ(tick(1200, false, true, false, true, true), A::ReaderMenu);
}
