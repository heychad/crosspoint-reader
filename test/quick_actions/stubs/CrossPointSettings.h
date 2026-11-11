#pragma once
#include <cstdint>

#include "util/HomeButtonInput.h"
struct CrossPointSettings {
  static constexpr uint8_t SCREEN_MARGIN_MIN = 5, SCREEN_MARGIN_MAX = 150;
  uint8_t screenMargin = SCREEN_MARGIN_MIN, screenMarginVertical = SCREEN_MARGIN_MIN;
  uint8_t powerTapAction = 0, powerHoldAction = 0, powerUpChordAction = 0, backHoldAction = 0, confirmHoldAction = 0;
  int saves = 0;
  bool failSave = false;
  bool saveToFile() {
    ++saves;
    return !failSave;
  }
  enum { IGNORE = 0, PWR_QUICK_ACTIONS = 10, LP_MENU_DISABLED = 1, LP_MENU_QUICK_ACTIONS = 7 };
  uint8_t readerUpTap = 0, readerUpHold = 0, readerDownTap = 0, readerDownHold = 0, readerSideChord = 0;
  uint8_t readerLeftEdgeUp = 0, readerLeftEdgeDown = 0, readerRightEdgeUp = 0, readerRightEdgeDown = 0;
  uint8_t homeButtonTapAction = static_cast<uint8_t>(HomeButtonAction::Home);
  uint8_t homeButtonDoubleTapAction = static_cast<uint8_t>(HomeButtonAction::ToggleFrontlight);
  uint8_t homeButtonLongPressAction = static_cast<uint8_t>(HomeButtonAction::ReaderMenu);
  uint8_t shortPwrBtn = IGNORE, longPressMenuFunction = LP_MENU_DISABLED;
};
