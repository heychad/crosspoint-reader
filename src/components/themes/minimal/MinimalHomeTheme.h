#pragma once

#include "HomeDashboardData.h"
#include "components/themes/lyra/LyraTheme.h"

namespace MinimalHomeMetrics {
constexpr ThemeMetrics makeValues() {
  auto values = LyraMetrics::values;
  values.homeCoverHeight = 583;
  values.homeCoverTileHeight = 744;
  return values;
}
constexpr ThemeMetrics values = makeValues();
}  // namespace MinimalHomeMetrics

class MinimalHomeTheme : public LyraTheme {
 public:
  static Rect coverRect(const GfxRenderer& renderer, Rect rect);
  int homeCoverThumbHeight(const GfxRenderer& renderer) const override;
  static void drawReadingInfo(GfxRenderer& renderer, Rect rect, const HomeDashboardData& data);
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
};
