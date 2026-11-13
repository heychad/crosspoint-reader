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
#include "components/themes/minimal/CompactHomeLayout.h"
#include "fontIds.h"

namespace {
compacthomelayout::Layout layoutFor(const GfxRenderer& renderer, Rect rect) {
  return compacthomelayout::dashboard(rect.x, rect.y, rect.width, rect.height,
                                      renderer.getLineHeight(uiScaleSpec().bodyFontId),
                                      renderer.getLineHeight(UI_10_FONT_ID));
}
void drawRow(GfxRenderer& renderer, compacthomelayout::Box box, StrId label, const char* value) {
  const int font = uiScaleSpec().bodyFontId;
  const auto text = renderer.truncatedText(font, value, box.width, EpdFontFamily::BOLD);
  const auto caption = renderer.truncatedText(UI_10_FONT_ID, I18n::getInstance().get(label), box.width);
  renderer.drawText(font, box.x, box.y, text.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, box.x, box.y + renderer.getLineHeight(font) + 2, caption.c_str());
}
void drawBookText(GfxRenderer& renderer, compacthomelayout::Box box, int lines, const RecentBook& book,
                  const char* chapter) {
  const int font = uiScaleSpec().bodyFontId;
  const auto title = renderer.wrappedText(font, book.title.c_str(), box.width, lines, EpdFontFamily::BOLD);
  int y = box.y;
  for (const auto& line : title) {
    renderer.drawText(font, box.x, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(font);
  }
  y += 8;
  const char* subtitle = chapter[0] ? chapter : book.author.c_str();
  const auto wrapped = renderer.wrappedText(UI_10_FONT_ID, subtitle, box.width, lines);
  for (const auto& line : wrapped) {
    if (y + renderer.getLineHeight(UI_10_FONT_ID) > box.y + box.height) break;
    renderer.drawText(UI_10_FONT_ID, box.x, y, line.c_str());
    y += renderer.getLineHeight(UI_10_FONT_ID);
  }
}
}  // namespace

Rect DashboardHomeTheme::coverRect(const GfxRenderer& renderer, Rect rect) {
  const auto layout = layoutFor(renderer, rect);
  return Rect{layout.cover.x, layout.cover.y, layout.cover.width, layout.cover.height};
}
int DashboardHomeTheme::homeCoverThumbHeight(const GfxRenderer& renderer) const {
  const auto content = UITheme::getInstance().getContentArea(renderer);
  const int top = UITheme::getInstance().getMetrics().homeTopPadding;
  return coverRect(renderer,
                   Rect{content.x, top, content.width, compacthomelayout::tileHeight(renderer.getScreenHeight(), top)})
      .height;
}
void DashboardHomeTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect,
                                             const std::vector<RecentBook>& recentBooks, int selectorIndex,
                                             bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                                             std::function<bool()> storeCoverBuffer) const {
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
    }
    renderer.maskRoundedRectOutsideCorners(cover.x, cover.y, cover.width, cover.height, 8, Color::White);
    renderer.drawRoundedRect(cover.x, cover.y, cover.width, cover.height, 1, 8, true);
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }
}
void DashboardHomeTheme::drawReadingInfo(GfxRenderer& renderer, Rect rect, const RecentBook& book,
                                         const HomeDashboardData& data, const HomeDashboardDetails& details) {
  const auto layout = layoutFor(renderer, rect);
  const int count = data.hasStats ? (details.hasClock ? 8 : 6) : 1;
  const auto row = [&](int index, StrId label, const char* value) {
    drawRow(renderer, compacthomelayout::cell(layout.stats, count == 1 ? 1 : layout.columns, count, index), label,
            value);
  };
  char value[40];
  if (data.progress >= 0)
    snprintf(value, sizeof(value), "%d%%", std::clamp(data.progress, 0, 100));
  else
    snprintf(value, sizeof(value), "-");
  if (!data.hasStats) {
    row(0, StrId::STR_STATS_PROGRESS, value);
  } else {
    row(2, StrId::STR_STATS_PROGRESS, value);
    BookReadingStats::formatDuration(data.readingSeconds, value, sizeof(value));
    row(0, StrId::STR_STATS_TOTAL_TIME, value);
    if (data.remainingSeconds)
      BookReadingStats::formatDuration(data.remainingSeconds, value, sizeof(value));
    else
      snprintf(value, sizeof(value), "-");
    row(1, StrId::STR_BAR_TIME_BOOK, value);
    if (details.hasPace)
      snprintf(value, sizeof(value), "%lu.%lu", static_cast<unsigned long>(details.pagesPerMinuteTenths / 10),
               static_cast<unsigned long>(details.pagesPerMinuteTenths % 10));
    else
      snprintf(value, sizeof(value), "-");
    row(details.hasClock ? 4 : 3, StrId::STR_STATS_PAGES_PER_MINUTE, value);
    if (details.hasClock) {
      if (details.hasDays)
        BookReadingStats::formatDuration(details.dailyAverageSeconds, value, sizeof(value));
      else
        snprintf(value, sizeof(value), "-");
      row(3, StrId::STR_STATS_DAILY_AVERAGE, value);
      if (details.hasDays)
        snprintf(value, sizeof(value), "%u", details.daysReading);
      else
        snprintf(value, sizeof(value), "-");
      row(5, StrId::STR_STATS_DAYS_READING, value);
      formatReadingStatsShortDate(details.startDate, value, sizeof(value));
      row(6, StrId::STR_STATS_STARTED, value);
      formatReadingStatsShortDate(details.finishDate, value, sizeof(value));
      row(7, details.completed ? StrId::STR_STATS_FINISHED : StrId::STR_STATS_PROJECTED_FINISH, value);
    } else {
      snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(details.sessions));
      row(4, StrId::STR_STATS_SESSIONS, value);
      if (details.sessions)
        BookReadingStats::formatDuration(details.averageSessionSeconds, value, sizeof(value));
      else
        snprintf(value, sizeof(value), "-");
      row(5, StrId::STR_STATS_AVERAGE_SESSION, value);
    }
  }
  drawBookText(renderer, layout.text, layout.textLines, book, details.chapter);
  if (!details.hasGlobal) return;
  const auto left = compacthomelayout::cell(layout.footer, 2, 2, 0);
  const auto right = compacthomelayout::cell(layout.footer, 2, 2, 1);
  if (details.hasClock) {
    snprintf(value, sizeof(value), "%u", details.currentStreak);
    drawRow(renderer, left, StrId::STR_STATS_CURRENT_STREAK, value);
    static constexpr StrId periods[] = {StrId::STR_STATS_MORNING, StrId::STR_STATS_AFTERNOON, StrId::STR_STATS_EVENING,
                                        StrId::STR_STATS_NIGHT};
    drawRow(renderer, right, StrId::STR_STATS_TIME_OF_DAY,
            details.hasReaderType ? I18n::getInstance().get(periods[static_cast<unsigned>(details.readerType)]) : "-");
  } else {
    BookReadingStats::formatDuration(details.globalSeconds, value, sizeof(value));
    drawRow(renderer, left, StrId::STR_STATS_TOTAL_TIME, value);
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(details.completedBooks));
    drawRow(renderer, right, StrId::STR_STATS_COMPLETED, value);
  }
}
