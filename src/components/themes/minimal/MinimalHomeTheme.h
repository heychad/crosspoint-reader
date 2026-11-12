#pragma once

#include "components/themes/lyra/LyraTheme.h"

namespace MinimalHomeMetrics {
constexpr ThemeMetrics makeValues() {
  auto values = LyraMetrics::values;
  values.homeCoverHeight = 300;
  values.homeCoverTileHeight = 390;
  return values;
}
constexpr ThemeMetrics values = makeValues();
}  // namespace MinimalHomeMetrics

class MinimalHomeTheme : public LyraTheme {
 public:
  static Rect coverRect(Rect rect);
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
};
