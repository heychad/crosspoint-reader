#pragma once
#include <array>

#include "util/HomeButtonInput.h"
class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right };
  std::array<bool, 4> down{}, pressed{}, released{};
  bool touch = true;
  uint32_t held = 0;
  uint32_t getHeldTime() const { return held; }
  bool isNavDirectionSwapped() const { return false; }
  HomeButtonAction home = HomeButtonAction::Ignore;
  HomeButtonInput::Gesture gesture = HomeButtonInput::Gesture::None;
  bool isPressed(Button b) const { return down[static_cast<int>(b)]; }
  bool wasPressed(Button b) const { return pressed[static_cast<int>(b)]; }
  bool wasReleased(Button b) const { return released[static_cast<int>(b)]; }
  bool hasTouch() const { return touch; }
  HomeButtonAction homeButtonAction() const { return home; }
  HomeButtonInput::Gesture homeButtonGesture() const { return gesture; }
};
