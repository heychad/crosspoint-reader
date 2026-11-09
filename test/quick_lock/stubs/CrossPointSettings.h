#pragma once
#include <cstdint>
struct CrossPointSettings {
  enum { OFF = 0 };
  uint8_t powerTapAction = 0, powerHoldAction = 0, powerUpChordAction = 0, backHoldAction = 0, confirmHoldAction = 0;
  uint8_t readerUpHold = 0, readerDownHold = 0, longPressButtonBehavior = 0, sideButtonLayout = 0;
  uint32_t timeout = 10000;
  uint32_t getSleepTimeoutMs() const { return timeout; }
};
extern CrossPointSettings settings;
#define SETTINGS settings
