#pragma once

#include "HomeDashboardData.h"
#include "MinimalHomeTheme.h"

class DashboardHomeTheme : public MinimalHomeTheme {
 public:
  static Rect coverRect(Rect rect);
  bool hasDashboardHome() const override { return true; }
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  static void drawReadingInfo(GfxRenderer& renderer, Rect rect, const HomeDashboardData& data);
};
