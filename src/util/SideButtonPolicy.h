#pragma once
#include <cstdint>

enum class SidePageDirection : uint8_t { None, Previous, Next };

inline SidePageDirection sidePageDirection(bool up, uint8_t layout, bool inverted) {
  switch (layout) {
    case 0:
      return up != inverted ? SidePageDirection::Previous : SidePageDirection::Next;
    case 1:
      return up != inverted ? SidePageDirection::Next : SidePageDirection::Previous;
    case 3:
      return SidePageDirection::Next;
    case 4:
      return SidePageDirection::Previous;
    default:
      return SidePageDirection::None;
  }
}

// A handled hold/chord remains owned by its origin through the release frame,
// even when it opens another activity or the reader toolbar.
class SideButtonContactGuard {
 public:
  void consume(bool up, bool down) { mask |= (up ? 1 : 0) | (down ? 2 : 0); }
  void update(bool up, bool down, bool upReleased, bool downReleased) {
    const uint8_t active = ((up || upReleased) ? 1 : 0) | ((down || downReleased) ? 2 : 0);
    if ((mask & active) == 0) mask = 0;
  }
  bool blocks(bool up) const { return (mask & (up ? 1 : 2)) != 0; }

 private:
  uint8_t mask = 0;
};
