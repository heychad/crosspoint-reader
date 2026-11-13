#include "MinimalHomeTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdio>

#include "RecentBooksStore.h"
#include "activities/reader/BookReadingStats.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/themes/minimal/CompactHomeLayout.h"
#include "fontIds.h"

Rect MinimalHomeTheme::coverRect(const GfxRenderer& renderer, Rect rect) {
  const auto layout =
      compacthomelayout::minimal(rect.x, rect.y, rect.width, rect.height, renderer.getLineHeight(UI_10_FONT_ID));
  return Rect{layout.cover.x, layout.cover.y, layout.cover.width, layout.cover.height};
}

int MinimalHomeTheme::homeCoverThumbHeight(const GfxRenderer& renderer) const {
  const auto content = UITheme::getInstance().getContentArea(renderer);
  const int top = UITheme::getInstance().getMetrics().homeTopPadding;
  return coverRect(renderer,
                   Rect{content.x, top, content.width, compacthomelayout::tileHeight(renderer.getScreenHeight(), top)})
      .height;
}

void MinimalHomeTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                           int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                           bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
  (void)selectorIndex;
  if (recentBooks.empty()) {
    drawEmptyRecents(renderer, rect);
    return;
  }
  const auto& book = recentBooks.front();
  const Rect cover = coverRect(renderer, rect);
  if (!coverRendered || !bufferRestored) {
    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
    bool drawn = false;
    if (!book.coverBmpPath.empty()) {
      HalFile file;
      const auto path = UITheme::getCoverThumbPath(book.coverBmpPath, cover.height);
      if (Storage.openFileForRead("HOME", path, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) drawn = drawCoverThumbFill(renderer, bitmap, cover);
      }
    }
    if (!drawn) {
      renderer.fillRect(cover.x, cover.y, cover.width, cover.height, false);
      drawCoverPlaceholder(renderer, cover);
      const int font = uiScaleSpec().bodyFontId;
      const auto title = renderer.truncatedText(font, book.title.c_str(), std::max(0, cover.width - 24));
      renderer.drawText(font, cover.x + 12, cover.y + 12, title.c_str(), true, EpdFontFamily::BOLD);
    }
    renderer.maskRoundedRectOutsideCorners(cover.x, cover.y, cover.width, cover.height, 8, Color::White);
    renderer.drawRoundedRect(cover.x, cover.y, cover.width, cover.height, 1, 8, true);
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }
}

void MinimalHomeTheme::drawReadingInfo(GfxRenderer& renderer, Rect rect, const HomeDashboardData& data) {
  const int line = renderer.getLineHeight(UI_10_FONT_ID);
  const auto layout = compacthomelayout::minimal(rect.x, rect.y, rect.width, rect.height, line);
  const auto& footer = layout.footer;
  char value[32];
  if (data.hasStats && data.readingSeconds) {
    BookReadingStats::formatDuration(data.readingSeconds, value, sizeof(value));
    const auto text = renderer.truncatedText(UI_10_FONT_ID, value, footer.width);
    renderer.drawText(UI_10_FONT_ID, footer.x, footer.y, text.c_str());
  }
  if (data.progress < 0) return;
  const int progress = std::clamp(data.progress, 0, 100);
  const int y = footer.y + line + 4;
  renderer.fillRectDither(footer.x, y, footer.width, 5, Color::LightGray);
  if (progress) renderer.fillRect(footer.x, y, footer.width * progress / 100, 5, true);
  snprintf(value, sizeof(value), "%d%%", progress);
  renderer.drawText(UI_10_FONT_ID, footer.x + footer.width - renderer.getTextWidth(UI_10_FONT_ID, value), y + 8, value);
}
