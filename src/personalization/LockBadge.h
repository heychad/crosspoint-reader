#pragma once

#include <GfxRenderer.h>

#include <algorithm>
#include <array>

// Small region only; callers serialize all framebuffer access with RenderLock.
class LockBadge {
 public:
  static constexpr int SIZE = 40;
  void show(const GfxRenderer& renderer) {
    int top = 0, right = 0, bottom = 0, left = 0;
    renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
    x = std::max(left, renderer.getScreenWidth() - right - SIZE);
    y = std::max(top, renderer.getScreenHeight() - bottom - SIZE);
    orientation = renderer.getOrientation();
    size = renderer.getRegionByteSize(x, y, SIZE, SIZE);
    valid = size > 0 && size <= pixels.size() && renderer.copyRegionToBuffer(x, y, SIZE, SIZE, pixels.data(), size);
    renderer.fillRect(x, y, SIZE, SIZE, false);
    renderer.drawRect(x + 13, y + 6, 14, 17, 2, true);
    renderer.fillRect(x + 8, y + 17, 24, 18, true);
    renderer.fillRect(x + 18, y + 22, 4, 8, false);
  }
  bool restore(const GfxRenderer& renderer) {
    const bool restoreValid = valid && renderer.getOrientation() == orientation;
    valid = false;
    return restoreValid && renderer.copyBufferToRegion(x, y, SIZE, SIZE, pixels.data(), size);
  }
  void invalidate() { valid = false; }

 private:
  std::array<uint8_t, 6 * SIZE> pixels{};
  int x = 0, y = 0;
  size_t size = 0;
  GfxRenderer::Orientation orientation = GfxRenderer::Orientation::Portrait;
  bool valid = false;
};
