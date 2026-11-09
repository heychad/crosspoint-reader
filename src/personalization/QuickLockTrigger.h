// Adapted from CrossInk v1.6.1; see docs/downstream/CROSSINK_NOTICE.md.
#pragma once

#include <cstdint>

// Records the one shortcut that may release an active Quick Lock.
enum class QuickLockTrigger : uint8_t {
  None = 0,
  ShortPower,
  LongPower,
  PowerUp,
  UpDown,
  LongBack,
  LongMenu,
  HomeDoubleTap,
  HomeTap,
  HomeLongPress,
  SideUpShort,
  SideUpLong,
  SideDownShort,
  SideDownLong,
};
