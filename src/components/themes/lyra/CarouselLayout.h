#pragma once

#include <algorithm>

namespace carousellayout {
struct Box {
  int x;
  int y;
  int width;
  int height;
  bool contains(int px, int py) const {
    return width > 0 && height > 0 && px >= x && py >= y && px - x < width && py - y < height;
  }
};
struct Layout {
  Box center;
  Box left;
  Box right;
};
inline int menuHeight(int labelHeight) { return std::max(64, 44 + labelHeight + 4); }
inline int coverHeight(int requested, int screenHeight, int top, int footerHeight, int labelHeight) {
  return std::max(0, std::min(requested, screenHeight - top - footerHeight - menuHeight(labelHeight) - 8));
}
inline Layout make(int x, int y, int width, int height, int titleHeight) {
  const int centerHeight = std::min(400, std::max(1, height - titleHeight * 4 - 46));
  const int centerWidth = std::min({300, std::max(1, width - 120), std::max(1, centerHeight * 3 / 5)});
  const int centerX = x + (width - centerWidth) / 2;
  const int centerY = y + titleHeight * 2 + 4;
  const int sideWidth = std::max(1, (width - centerWidth) / 2 - 8);
  const int sideHeight = centerHeight * 3 / 4;
  const int sideY = centerY + (centerHeight - sideHeight) / 2;
  return {{centerX, centerY, centerWidth, centerHeight},
          {x, sideY, sideWidth, sideHeight},
          {x + width - sideWidth, sideY, sideWidth, sideHeight}};
}
inline int hit(const Layout& layout, int x, int y, int count, int center) {
  if (count <= 0 || center < 0 || center >= count) return -1;
  if (layout.center.contains(x, y)) return center;
  if (count > 1 && layout.left.contains(x, y)) return (center + count - 1) % count;
  if (count > 2 && layout.right.contains(x, y)) return (center + 1) % count;
  return -1;
}
inline Box menuTile(int x, int y, int width, int height, int count, int index) {
  if (count <= 0 || index < 0 || index >= count || width <= 0 || height <= 0) return {x, y, 0, 0};
  const int left = x + width * index / count;
  const int right = x + width * (index + 1) / count;
  return {left, y, right - left, height};
}
inline int menuHit(int x, int y, int width, int height, int count, int px, int py) {
  for (int i = 0; i < count; ++i) {
    if (menuTile(x, y, width, height, count, i).contains(px, py)) return i;
  }
  return -1;
}
}  // namespace carousellayout
