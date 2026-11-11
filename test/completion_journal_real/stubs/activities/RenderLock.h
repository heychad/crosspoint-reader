#pragma once
struct RenderLock {
  enum class Mode { Try };
  explicit RenderLock(Mode) {}
  bool ownsLock() const { return true; }
};
