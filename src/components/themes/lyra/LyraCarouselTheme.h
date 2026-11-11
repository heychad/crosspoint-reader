#pragma once

#include "components/themes/lyra/LyraTheme.h"
#include "components/themes/minimal/HomeDashboardData.h"

namespace LyraCarouselMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics metrics = LyraMetrics::values;
  metrics.homeCoverHeight = 400;
  metrics.homeCoverTileHeight = 480;
  metrics.homeRecentBooksCount = 3;
  return metrics;
}();
}  // namespace LyraCarouselMetrics

class LyraCarouselTheme : public LyraTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  int homeCoverThumbHeight(const GfxRenderer& renderer) const override;
  static void drawReadingProgress(GfxRenderer& renderer, Rect rect, const HomeDashboardData& data);
  static void drawHomeMenu(GfxRenderer& renderer, Rect rect, int count, int selected,
                           const std::function<std::string(int)>& label, const std::function<UIIcon(int)>& icon);

 private:
  mutable int centerIndex = 0;
  mutable int cachedIndex = -1;
};
