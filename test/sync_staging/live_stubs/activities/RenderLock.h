#pragma once
inline bool renderBusy = false, renderHeld = false;
class RenderLock {
 public:
  enum class Mode { Try };
  explicit RenderLock(Mode) : owns(!renderBusy) {
    if (owns) renderHeld = true;
  }
  ~RenderLock() {
    if (owns) renderHeld = false;
  }
  bool ownsLock() const { return owns; }

 private:
  bool owns;
};
