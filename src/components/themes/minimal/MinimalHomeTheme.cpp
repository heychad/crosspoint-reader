#include "MinimalHomeTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>

#include "RecentBooksStore.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "fontIds.h"

Rect MinimalHomeTheme::coverRect(Rect rect) {
  const int coverHeight = std::max(1, std::min(MinimalHomeMetrics::values.homeCoverHeight, rect.height - 50));
  const int coverWidth = std::max(1, std::min(rect.width - 12, coverHeight * 3 / 5));
  return Rect{rect.x + (rect.width - coverWidth) / 2, rect.y + 8, coverWidth, coverHeight};
}

void MinimalHomeTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                           int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                           bool& bufferRestored, std::function<bool()> storeCoverBuffer) const {
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
