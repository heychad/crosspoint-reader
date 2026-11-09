#include <gtest/gtest.h>

#include "personalization/HoldShortcutController.h"
using C = HoldShortcutController;
using A = C::Action;
TEST(HoldShortcuts, PowerTapFiresOnlyOnRelease) {
  C c;
  C::Config cfg;
  cfg.bindings[0] = {A::QuickLock, A::Sleep, 400};
  C::Frame f;
  f.contacts[0] = {true, true, false};
  EXPECT_EQ(c.update(f, cfg).action, A::Default);
  f.now = 50;
  f.contacts[0] = {false, false, true};
  auto event = c.update(f, cfg);
  EXPECT_EQ(event.action, A::QuickLock);
  EXPECT_EQ(event.trigger, QuickLockTrigger::ShortPower);
  EXPECT_TRUE(event.consumed);
  f.contacts[0] = {};
  EXPECT_FALSE(c.update(f, cfg).consumed);
}
TEST(HoldShortcuts, PowerHoldSuppressesTapAndOnlyFiresOnce) {
  C c;
  C::Config cfg;
  cfg.bindings[0] = {A::QuickActions, A::QuickLock, 400};
  C::Frame f;
  f.contacts[0] = {true, true, false};
  c.update(f, cfg);
  f.now = 400;
  f.contacts[0] = {true, false, false};
  EXPECT_EQ(c.update(f, cfg).action, A::QuickLock);
  f.now = 500;
  EXPECT_EQ(c.update(f, cfg).action, A::Default);
  f.contacts[0] = {false, false, true};
  EXPECT_EQ(c.update(f, cfg).action, A::Default);
}
TEST(HoldShortcuts, InheritedContactAndContextChangeCannotFire) {
  C c;
  C::Config cfg;
  cfg.bindings[0] = {A::QuickActions, A::QuickLock, 400};
  C::Frame f;
  f.contacts[0] = {true, false, false};
  EXPECT_TRUE(c.update(f, cfg).consumed);
  f.now = 900;
  EXPECT_EQ(c.update(f, cfg).action, A::Default);
  f.contacts[0] = {false, false, true};
  c.update(f, cfg);
  f.contacts[0] = {true, true, false};
  c.update(f, cfg);
  f.visit = 1;
  f.now = 1500;
  f.contacts[0] = {true, false, false};
  EXPECT_EQ(c.update(f, cfg).action, A::Default);
  f.contacts[0] = {false, false, true};
  EXPECT_EQ(c.update(f, cfg).action, A::Default);
}
TEST(HoldShortcuts, ChordOwnerCancelsPendingPowerTap) {
  C c;
  C::Config cfg;
  cfg.bindings[0] = {A::QuickActions, A::QuickLock, 400};
  C::Frame f;
  f.contacts[0] = {true, true, false};
  c.update(f, cfg);
  f.blocked = true;
  f.contacts[0] = {true, false, false};
  c.update(f, cfg);
  f.blocked = false;
  f.contacts[0] = {false, false, true};
  EXPECT_EQ(c.update(f, cfg).action, A::Default);
}
TEST(HoldShortcuts, BackHoldOwnsPressAndHandlesClockWrap) {
  C c;
  C::Config cfg;
  cfg.bindings[1] = {A::Default, A::QuickLock, 1000};
  C::Frame f;
  f.now = UINT32_MAX - 500;
  f.contacts[1] = {true, true, false};
  EXPECT_TRUE(c.update(f, cfg).consumed);
  f.now = 600;
  f.contacts[1] = {true, false, false};
  auto result = c.update(f, cfg);
  EXPECT_EQ(result.action, A::QuickLock);
  EXPECT_EQ(result.trigger, QuickLockTrigger::LongBack);
}
