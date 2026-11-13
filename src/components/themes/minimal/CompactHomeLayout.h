#pragma once

#include <algorithm>

namespace compacthomelayout {
struct Box {
  int x, y, width, height;
};
struct Layout {
  Box cover, stats, text, footer;
  int columns = 1;
  int textLines = 2;
};
inline int tileHeight(int screenHeight, int top) { return std::max(0, screenHeight - top - 12); }
inline Layout minimal(int x, int y, int width, int height, int line) {
  const int footer = 2 * line + 26;
  const int available = std::max(0, height - 24 - footer);
  const int coverH = std::max(0, std::min({583, available, (width - 24) * 5 / 3}));
  const int coverW = coverH * 3 / 5;
  const Box cover{x + (width - coverW) / 2, y + 12 + (available - coverH) / 2, coverW, coverH};
  return {cover, {}, {}, {cover.x, cover.y + coverH + 8, coverW, footer}};
}
inline Layout dashboard(int x, int y, int width, int height, int titleLine, int smallLine) {
  const bool wide = width > height;
  const int lines = wide ? 1 : 2;
  const int textH = lines * (titleLine + smallLine) + 8;
  const int footerH = titleLine + smallLine + 6;
  const int artH = std::max(0, height - 24 - textH - footerH - 24);
  const int coverH = std::max(0, std::min({468, artH, (width * 2 / 3 - 36) * 5 / 3}));
  const int coverW = coverH * 3 / 5;
  const Box cover{x + 12, y + 12, coverW, coverH};
  const int statsX = cover.x + coverW + 16;
  return {cover,
          {statsX, y + 12, std::max(0, x + width - 12 - statsX), artH},
          {x + 12, y + 12 + artH + 12, width - 24, textH},
          {x + 12, y + height - 12 - footerH, width - 24, footerH},
          wide ? 3 : 1,
          lines};
}
inline Box cell(Box area, int columns, int count, int index) {
  const int rows = std::max(1, (count + columns - 1) / columns);
  const int column = index % columns, row = index / columns;
  const int left = area.x + area.width * column / columns;
  const int right = area.x + area.width * (column + 1) / columns;
  return {left, area.y + area.height * row / rows, std::max(0, right - left - 8), area.height / rows};
}
}  // namespace compacthomelayout
