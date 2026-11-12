#include "DashboardHomeTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "RecentBooksStore.h"
#include "activities/reader/BookReadingStats.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "fontIds.h"

Rect DashboardHomeTheme::coverRect(Rect rect) {
  const int height = std::max(1, std::min(MinimalHomeMetrics::values.homeCoverHeight, rect.height - 50));
  const int width = std::max(1, std::min(rect.width / 2 - 20, height * 3 / 5));
  return Rect{rect.x + 12, rect.y + 8, width, height};
}

void DashboardHomeTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                             const std::vector<RecentBook>& recentBooks, int selectorIndex,
                                             bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                             std::function<bool()> storeCoverBuffer) const {
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }
  const auto& book = recentBooks.front();
  const Rect cover = coverRect(rect);
  if (!coverRendered || !bufferRestored) {
    bool drawn = false;
    if (!book.coverBmpPath.empty()) {
      HalFile file;
      const auto path = UITheme::getCoverThumbPath(book.coverBmpPath, MinimalHomeMetrics::values.homeCoverHeight);
      if (Storage.openFileForRead("HOME", path, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) drawn = drawCoverThumbFill(renderer, bitmap, cover);
        file.close();
      }
    }
    if (!drawn) drawCoverPlaceholder(renderer, cover);
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }
  if (selectorIndex == 0) renderer.drawRect(cover.x - 3, cover.y - 3, cover.width + 6, cover.height + 6, true);
  const int textWidth = std::max(1, rect.width - 24);
  const int bodyFont = uiScaleSpec().bodyFontId;
  const auto title = renderer.truncatedText(bodyFont, book.title.c_str(), textWidth, EpdFontFamily::BOLD);
  const int titleY = cover.y + cover.height + 10;
  renderer.drawCenteredText(bodyFont, titleY, title.c_str(), true, EpdFontFamily::BOLD);
  if (!book.author.empty()) {
    const auto author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);
    renderer.drawCenteredText(UI_10_FONT_ID, titleY + renderer.getLineHeight(bodyFont) + 4, author.c_str());
  }
}

void DashboardHomeTheme::drawReadingInfo(GfxRenderer& renderer, Rect rect, const HomeDashboardData& data) {
  const Rect cover = coverRect(rect);
  const int x = cover.x + cover.width + 18;
  const int width = rect.x + rect.width - x - 12;
  if (width <= 0) return;
  char value[40];
  const auto row = [&](int y, StrId label, const char* text) {
    const auto truncatedLabel = renderer.truncatedText(UI_10_FONT_ID, I18n::getInstance().get(label), width);
    const auto truncatedValue = renderer.truncatedText(UI_12_FONT_ID, text, width, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, x, y, truncatedLabel.c_str());
    renderer.drawText(UI_12_FONT_ID, x, y + renderer.getLineHeight(UI_10_FONT_ID) + 4, truncatedValue.c_str(), true,
                      EpdFontFamily::BOLD);
  };
  if (data.progress >= 0)
    snprintf(value, sizeof(value), "%d%%", data.progress);
  else
    snprintf(value, sizeof(value), "-");
  row(cover.y + 16, StrId::STR_STATS_PROGRESS, value);
  if (data.hasStats)
    BookReadingStats::formatDuration(data.readingSeconds, value, sizeof(value));
  else
    snprintf(value, sizeof(value), "-");
  row(cover.y + 96, StrId::STR_STATS_TOTAL_TIME, value);
  if (data.hasStats && data.remainingSeconds)
    BookReadingStats::formatDuration(data.remainingSeconds, value, sizeof(value));
  else
    snprintf(value, sizeof(value), "-");
  row(cover.y + 176, StrId::STR_BAR_TIME_BOOK, value);
}
