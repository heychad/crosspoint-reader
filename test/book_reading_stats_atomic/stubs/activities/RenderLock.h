#pragma once
inline bool testRenderLockAvailable = true;
struct RenderLock {
  enum class Mode { Try };
  explicit RenderLock(Mode) {}
  bool ownsLock() const { return testRenderLockAvailable; }
};
