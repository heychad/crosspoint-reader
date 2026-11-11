#include "LyraCarouselTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdio>

#include "RecentBooksStore.h"
#include "activities/reader/BookReadingStats.h"
#include "components/UITheme.h"
#include "components/icons/blocks.h"
#include "components/icons/bookmark.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/library.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "components/themes/lyra/CarouselLayout.h"
#include "fontIds.h"

void LyraCarouselTheme::drawHomeMenu(GfxRenderer& renderer, Rect rect, int count, int selected,
                                     const std::function<std::string(int)>& label,
                                     const std::function<UIIcon(int)>& icon) {
  for (int i = 0; i < count; ++i) {
    const auto tile = carousellayout::menuTile(rect.x, rect.y, rect.width, rect.height, count, i);
    const uint8_t* bitmap = nullptr;
    switch (icon(i)) {
      case UIIcon::Folder:
        bitmap = FolderIcon;
        break;
      case UIIcon::Library:
        bitmap = LibraryIcon;
        break;
      case UIIcon::Bookmark:
        bitmap = BookmarkIcon;
        break;
      case UIIcon::Transfer:
        bitmap = TransferIcon;
        break;
      case UIIcon::Settings:
        bitmap = Settings2Icon;
        break;
      case UIIcon::Plugins:
        bitmap = BlocksIcon;
        break;
      default:
        break;
    }
    const int iconX = tile.x + (tile.width - 32) / 2;
    if (bitmap) renderer.drawIcon(bitmap, iconX, tile.y + 5, 32);
    if (i == selected) {
      renderer.drawRoundedRect(iconX - 4, tile.y + 1, 40, 40, 1, 5, true);
      const auto text = renderer.truncatedText(SMALL_FONT_ID, label(i).c_str(), tile.width);
      renderer.drawText(SMALL_FONT_ID, tile.x + (tile.width - renderer.getTextWidth(SMALL_FONT_ID, text.c_str())) / 2,
                        tile.y + 44, text.c_str(), true);
    }
  }
}

int LyraCarouselTheme::homeCoverThumbHeight(const GfxRenderer& renderer) const {
  const Rect content = UITheme::getInstance().getContentArea(renderer);
  const int width = std::min(300, std::max(1, content.width - 120));
  return std::max(LyraCarouselMetrics::values.homeCoverHeight, width * 5 / 3 + 2);
}

void LyraCarouselTheme::drawReadingProgress(GfxRenderer& renderer, Rect rect, const HomeDashboardData& data) {
  if (data.progress < 0 && !data.hasStats) return;
  const int titleHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const auto layout = carousellayout::make(rect.x, rect.y, rect.width, rect.height, titleHeight);
  int y = layout.center.y + layout.center.height + 23;
  const int width = layout.center.width;
  if (width <= 0) return;
  if (data.hasStats && y + titleHeight <= rect.y + rect.height) {
    char duration[32];
    BookReadingStats::formatDuration(data.readingSeconds, duration, sizeof(duration));
    renderer.drawText(SMALL_FONT_ID, layout.center.x, y, duration, true);
    y += titleHeight + 3;
  }
  if (data.progress < 0 || y + titleHeight + 6 > rect.y + rect.height) return;
  char label[8];
  const int value = std::clamp(data.progress, 0, 100);
  snprintf(label, sizeof(label), "%d%%", value);
  renderer.fillRectDither(layout.center.x, y, width, 4, Color::LightGray);
  if (value) renderer.fillRect(layout.center.x, y, width * value / 100, 4, true);
  renderer.drawText(SMALL_FONT_ID, layout.center.x + width - renderer.getTextWidth(SMALL_FONT_ID, label), y + 6, label,
                    true);
}

void LyraCarouselTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                            const std::vector<RecentBook>& recentBooks, int selectorIndex,
                                            bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                            std::function<bool()> storeCoverBuffer) const {
  const int count = static_cast<int>(recentBooks.size());
  if (!count) {
    cachedIndex = -1;
    drawEmptyRecents(renderer, rect);
    return;
  }
  if (selectorIndex >= 0 && selectorIndex < count) centerIndex = selectorIndex;
  centerIndex = std::clamp(centerIndex, 0, count - 1);
  if (centerIndex != cachedIndex) {
    coverRendered = false;
    bufferRestored = false;
  }
  const int titleHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const auto layout = carousellayout::make(rect.x, rect.y, rect.width, rect.height, titleHeight);
  const Rect center{layout.center.x, layout.center.y, layout.center.width, layout.center.height};
  if (!coverRendered || !bufferRestored) {
    renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);
    const auto drawCover = [&](int index, Rect slot) {
      bool drawn = false;
      const auto& book = recentBooks[index];
      if (!book.coverBmpPath.empty()) {
        const auto path = UITheme::getCoverThumbPath(book.coverBmpPath, homeCoverThumbHeight(renderer));
        HalFile file;
        if (Storage.openFileForRead("HOME", path, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) drawn = drawCoverThumbFill(renderer, bitmap, slot);
          file.close();
        }
      }
      if (!drawn) renderer.drawIcon(CoverIcon, slot.x + 8, slot.y + 8, 32);
      renderer.drawRect(slot.x, slot.y, slot.width, slot.height, true);
    };
    if (count > 1)
      drawCover((centerIndex + count - 1) % count,
                Rect{layout.left.x, layout.left.y, layout.left.width, layout.left.height});
    if (count > 2)
      drawCover((centerIndex + 1) % count,
                Rect{layout.right.x, layout.right.y, layout.right.width, layout.right.height});
    drawCover(centerIndex, center);
    const auto lines = renderer.wrappedText(SMALL_FONT_ID, recentBooks[centerIndex].title.c_str(), rect.width, 2);
    int titleY = rect.y;
    for (const auto& line : lines) {
      const int x = rect.x + (rect.width - renderer.getTextWidth(SMALL_FONT_ID, line.c_str())) / 2;
      renderer.drawText(SMALL_FONT_ID, x, titleY, line.c_str(), true);
      titleY += titleHeight;
    }
    const int dotsY = center.y + center.height + 8;
    int dotsX = rect.x + (rect.width - (count * 8 + (count - 1) * 6)) / 2;
    for (int i = 0; i < count; ++i, dotsX += 14) {
      if (i == centerIndex)
        renderer.fillRect(dotsX, dotsY, 8, 8, true);
      else
        renderer.drawRect(dotsX, dotsY, 8, 8, true);
    }
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
    cachedIndex = centerIndex;
  }
  if (selectorIndex == centerIndex)
    renderer.drawRect(center.x - 2, center.y - 2, center.width + 4, center.height + 4, true);
}
