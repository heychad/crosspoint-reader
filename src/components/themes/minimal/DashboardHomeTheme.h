#pragma once

#include "HomeDashboardData.h"
#include "HomeDashboardDetails.h"
#include "MinimalHomeTheme.h"

class DashboardHomeTheme : public MinimalHomeTheme {
 public:
  static Rect coverRect(const GfxRenderer& renderer, Rect rect);
  int homeCoverThumbHeight(const GfxRenderer& renderer) const override;
  bool hasDashboardHome() const override { return true; }
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           std::function<bool()> storeCoverBuffer) const override;
  static void drawReadingInfo(GfxRenderer& renderer, Rect rect, const RecentBook& book, const HomeDashboardData& data,
                              const HomeDashboardDetails& details);
};
