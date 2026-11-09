#pragma once
#include <array>
#include <cstdint>
class HalGPIO {
 public:
  enum { BTN_POWER, BTN_UP, BTN_DOWN };
  std::array<bool, 3> down{}, pressed{}, released{};
  uint32_t held = 0;
  bool home = false;
  bool isPressed(uint8_t b) const { return down[b]; }
  bool wasPressed(uint8_t b) const { return pressed[b]; }
  bool wasReleased(uint8_t b) const { return released[b]; }
  uint32_t getPowerButtonHeldTime() const { return held; }
  bool isHomeKeyDown() const { return home; }
};
