// Adapted from CrossInk's reading-statistics sleep screen; see CROSSINK_NOTICE.md.
#include "ReadingStatsSleep.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "activities/reader/ReadingChartModel.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace sleepstats {
namespace {
// Fixed scratch avoids retaining an unbounded book title or translation on the sleep path.
void textInBox(const GfxRenderer& renderer, int font, int x, int y, int width, const char* text, bool centered = false,
               EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  if (!text || width <= 0) return;
  char visible[96];
  const size_t count = strnlen(text, sizeof(visible) - 1);
  memcpy(visible, text, count);
  size_t length = static_cast<size_t>(utf8SafeTruncateBuffer(visible, static_cast<int>(count)));
  visible[length] = '\0';
  while (length > 0 && renderer.getTextWidth(font, visible, style) > width) {
    --length;
    while (length > 0 && (static_cast<unsigned char>(visible[length]) & 0xc0) == 0x80) --length;
    visible[length] = '\0';
  }
  const int left = centered ? x + (width - renderer.getTextWidth(font, visible, style)) / 2 : x;
  renderer.drawText(font, left, y, visible, true, style);
}

void statCell(const GfxRenderer& renderer, int x, int y, int width, int height, const char* label, const char* value) {
  const int small = renderer.getLineHeight(SMALL_FONT_ID);
  const int body = renderer.getLineHeight(UI_10_FONT_ID);
  const int top = y + std::max(0, (height - small - body - 2) / 2);
  textInBox(renderer, UI_10_FONT_ID, x + 3, top, width - 6, value, true, EpdFontFamily::BOLD);
  textInBox(renderer, SMALL_FONT_ID, x + 3, top + body + 2, width - 6, label, true);
}

void dateValue(const ReadingStatsDate& date, char* output, size_t size) {
  if (date.isValid())
    snprintf(output, size, "%04u-%02u-%02u", static_cast<unsigned>(date.year), static_cast<unsigned>(date.month),
             static_cast<unsigned>(date.day));
  else
    snprintf(output, size, "-");
}

void bookCard(const GfxRenderer& renderer, int x, int y, int width, int height, const char* title, const Data& data) {
  const int titleHeight = renderer.getLineHeight(UI_10_FONT_ID) + 10;
  const int rowCount = data.hasClock ? 3 : 2;
  const int rowHeight = (height - titleHeight) / rowCount;
  const int third = width / 3;
  const int half = width / 2;
  renderer.drawRect(x, y, width, height);
  renderer.drawLine(x, y + titleHeight, x + width - 1, y + titleHeight);
  textInBox(renderer, UI_10_FONT_ID, x + 8, y + 5, width - 16, title, true, EpdFontFamily::BOLD);
  char value[32];
  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(data.book.sessionCount));
  statCell(renderer, x, y + titleHeight, third, rowHeight, tr(STR_STATS_SESSIONS), value);
  BookReadingStats::formatDuration(data.book.totalReadingSeconds, value, sizeof(value));
  statCell(renderer, x + third, y + titleHeight, third, rowHeight, tr(STR_STATS_TOTAL_TIME), value);
  if (data.progress >= 0)
    snprintf(value, sizeof(value), "%d%%", data.progress);
  else
    snprintf(value, sizeof(value), "-");
  statCell(renderer, x + 2 * third, y + titleHeight, width - 2 * third, rowHeight, tr(STR_STATS_PROGRESS), value);
  BookReadingStats::formatDuration(data.book.sessionCount ? data.book.totalReadingSeconds / data.book.sessionCount : 0,
                                   value, sizeof(value));
  statCell(renderer, x, y + titleHeight + rowHeight, third, rowHeight, tr(STR_STATS_AVERAGE_SESSION), value);
  const uint32_t remaining = data.book.isCompleted ? 0 : data.book.remainingReadingSeconds(data.progress * 100);
  if (remaining)
    BookReadingStats::formatDuration(remaining, value, sizeof(value));
  else
    snprintf(value, sizeof(value), "-");
  statCell(renderer, x + third, y + titleHeight + rowHeight, third, rowHeight, tr(STR_BAR_TIME_BOOK), value);
  const uint64_t rate = data.book.totalReadingSeconds
                            ? static_cast<uint64_t>(data.book.totalPagesTurned) * 600 / data.book.totalReadingSeconds
                            : 0;
  snprintf(value, sizeof(value), "%llu.%llu", static_cast<unsigned long long>(rate / 10),
           static_cast<unsigned long long>(rate % 10));
  statCell(renderer, x + 2 * third, y + titleHeight + rowHeight, width - 2 * third, rowHeight,
           tr(STR_STATS_PAGES_PER_MINUTE), value);
  if (data.hasClock) {
    dateValue(data.book.startDate, value, sizeof(value));
    statCell(renderer, x, y + titleHeight + 2 * rowHeight, half, rowHeight, tr(STR_STATS_STARTED), value);
    const auto finish =
        data.book.isCompleted ? data.book.finishedDate : data.book.projectedFinishDate(data.now, data.progress * 100);
    dateValue(finish, value, sizeof(value));
    statCell(renderer, x + half, y + titleHeight + 2 * rowHeight, width - half, rowHeight,
             data.book.isCompleted ? tr(STR_STATS_FINISHED) : tr(STR_STATS_PROJECTED_FINISH), value);
  }
}

void bars(const GfxRenderer& renderer, int x, int y, int width, int height, const char* heading, const uint32_t* values,
          const StrId* labels, int count) {
  renderer.drawRect(x, y, width, height);
  const int headingHeight = renderer.getLineHeight(UI_10_FONT_ID) + 8;
  textInBox(renderer, UI_10_FONT_ID, x + 6, y + 4, width - 12, heading, true, EpdFontFamily::BOLD);
  const int rowHeight = (height - headingHeight - 4) / count;
  const int labelWidth = (width - 16) / 2;
  const int barWidth = width - 20 - labelWidth;
  const uint32_t maximum = *std::max_element(values, values + count);
  for (int i = 0; i < count; ++i) {
    const int rowY = y + headingHeight + i * rowHeight;
    textInBox(renderer, SMALL_FONT_ID, x + 6, rowY, labelWidth, I18N.get(labels[i]));
    const int fill = readingcharts::barWidth(values[i], maximum, barWidth);
    if (fill > 0) renderer.fillRect(x + 10 + labelWidth, rowY + std::max(1, rowHeight / 3), fill, 3);
  }
}

void globalCard(const GfxRenderer& renderer, int x, int y, int width, int height, const char* title,
                const GlobalReadingStats& stats, bool available, bool partial = false) {
  const int headingHeight =
      renderer.getLineHeight(UI_10_FONT_ID) + 10 + (partial ? renderer.getLineHeight(SMALL_FONT_ID) + 4 : 0);
  const int third = width / 3;
  renderer.drawRect(x, y, width, height);
  textInBox(renderer, UI_10_FONT_ID, x + 8, y + 5, width - 16, title, true, EpdFontFamily::BOLD);
  if (partial)
    textInBox(renderer, SMALL_FONT_ID, x + 8, y + renderer.getLineHeight(UI_10_FONT_ID) + 7, width - 16,
              tr(STR_STATS_PARTIAL), true);
  char value[32];
  if (available)
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(stats.totalSessions));
  else
    snprintf(value, sizeof(value), "-");
  statCell(renderer, x, y + headingHeight, third, height - headingHeight, tr(STR_STATS_SESSIONS), value);
  if (available)
    BookReadingStats::formatDuration(stats.totalReadingSeconds, value, sizeof(value));
  else
    snprintf(value, sizeof(value), "-");
  statCell(renderer, x + third, y + headingHeight, third, height - headingHeight, tr(STR_STATS_TOTAL_TIME), value);
  if (available)
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(stats.totalPagesTurned));
  else
    snprintf(value, sizeof(value), "-");
  statCell(renderer, x + 2 * third, y + headingHeight, width - 2 * third, height - headingHeight,
           tr(STR_STATS_PAGE_TURNS), value);
}
}  // namespace

bool draw(GfxRenderer& renderer, const char* title, const Data& data) {
  const Rect safe = UITheme::getContentArea(renderer);
  const int margin = std::clamp(safe.width / 30, 8, 24);
  const int width = safe.width - 2 * margin;
  const int line = renderer.getLineHeight(UI_10_FONT_ID);
  const int small = renderer.getLineHeight(SMALL_FONT_ID);
  const int headingHeight = line + 12;
  const int bookHeight = line + 10 + (data.hasClock ? 3 : 2) * (line + small + 10);
  const int remaining = safe.height - margin * 2 - headingHeight - bookHeight - 12;
  const bool showAllDevices = !data.hasClock && data.hasAllDevices;
  const int minimum = showAllDevices ? 2 * (2 * line + 2 * small + 18) + 8 : 7 * (small + 2) + line + 12;
  if (width < 220 || line <= 0 || small <= 0 || remaining < minimum) return false;
  renderer.clearScreen();
  const int x = safe.x + margin;
  int y = safe.y + margin;
  textInBox(renderer, UI_10_FONT_ID, x, y, width, tr(STR_READING_STATISTICS), true, EpdFontFamily::BOLD);
  y += headingHeight;
  bookCard(renderer, x, y, width, bookHeight, title, data);
  y += bookHeight + 12;
  if (data.hasClock) {
    static constexpr StrId times[] = {StrId::STR_STATS_MORNING, StrId::STR_STATS_AFTERNOON, StrId::STR_STATS_EVENING,
                                      StrId::STR_STATS_NIGHT};
    static constexpr StrId days[] = {StrId::STR_STATS_MONDAY,   StrId::STR_STATS_TUESDAY, StrId::STR_STATS_WEDNESDAY,
                                     StrId::STR_STATS_THURSDAY, StrId::STR_STATS_FRIDAY,  StrId::STR_STATS_SATURDAY,
                                     StrId::STR_STATS_SUNDAY};
    const int half = (width - 8) / 2;
    bars(renderer, x, y, half, remaining, tr(STR_STATS_TIME_OF_DAY), data.book.timeOfDaySeconds.data(), times, 4);
    bars(renderer, x + half + 8, y, width - half - 8, remaining, tr(STR_STATS_WEEKDAYS),
         data.book.dayOfWeekSeconds.data(), days, 7);
  } else {
    const int deviceHeight = showAllDevices ? (remaining - 8) / 2 : remaining;
    globalCard(renderer, x, y, width, deviceHeight, tr(STR_STATS_DEVICE), data.device, data.hasDevice);
    if (showAllDevices)
      globalCard(renderer, x, y + deviceHeight + 8, width, remaining - deviceHeight - 8, tr(STR_STATS_ALL_DEVICES),
                 data.allDevices.stats, data.allDevices.sources > 0, !data.allDevices.complete);
  }
  return true;
}
bool drawSummary(GfxRenderer& renderer, const char* title, const Data& data) {
  const Rect safe = UITheme::getContentArea(renderer);
  const int margin = std::clamp(safe.width / 30, 8, 24);
  const int width = safe.width - margin * 2;
  const int line = renderer.getLineHeight(UI_10_FONT_ID);
  const int large = renderer.getLineHeight(UI_12_FONT_ID);
  const int small = renderer.getLineHeight(SMALL_FONT_ID);
  const int cellTop = safe.y + safe.height / 4 + line * 2 + large + 68;
  const int cellHeight = line * 3 + 12;
  if (width < 220 || line <= 0 || large <= 0 || small <= 0 || cellHeight < small + line + 2 ||
      cellTop + cellHeight > safe.y + safe.height - margin)
    return false;

  renderer.clearScreen();
  const int x = safe.x + margin;
  int y = safe.y + safe.height / 4;
  textInBox(renderer, UI_10_FONT_ID, x, y, width, title, true, EpdFontFamily::BOLD);
  y += line + 30;
  char value[32];
  if (data.progress >= 0)
    snprintf(value, sizeof(value), "%d%%", data.progress);
  else
    snprintf(value, sizeof(value), "-");
  textInBox(renderer, UI_12_FONT_ID, x, y, width, value, true, EpdFontFamily::BOLD);
  y += large + 8;
  textInBox(renderer, UI_10_FONT_ID, x, y, width, tr(STR_STATS_PROGRESS), true);
  y += line + 30;
  const int half = width / 2;
  BookReadingStats::formatDuration(data.book.totalReadingSeconds, value, sizeof(value));
  statCell(renderer, x, y, half, cellHeight, tr(STR_STATS_TOTAL_TIME), value);
  const uint32_t remaining =
      data.progress >= 0 && !data.book.isCompleted ? data.book.remainingReadingSeconds(data.progress * 100) : 0;
  if (remaining)
    BookReadingStats::formatDuration(remaining, value, sizeof(value));
  else
    snprintf(value, sizeof(value), "-");
  statCell(renderer, x + half, y, width - half, cellHeight, tr(STR_BAR_TIME_BOOK), value);
  return true;
}
bool drawCoverSummary(GfxRenderer& renderer, const char* title, const Data& data, const Bitmap& cover) {
  const Rect safe = UITheme::getContentArea(renderer);
  const int margin = std::clamp(safe.width / 30, 8, 24);
  const int width = safe.width - margin * 2;
  const int line = renderer.getLineHeight(UI_10_FONT_ID);
  const int large = renderer.getLineHeight(UI_12_FONT_ID);
  const int small = renderer.getLineHeight(SMALL_FONT_ID);
  const int cellHeight = line * 3 + 12;
  const int coverHeight = std::min(300, safe.height - 2 * margin - (line * 5 + large + 64));
  if (width < 220 || line <= 0 || large <= 0 || small <= 0 || cellHeight < small + line + 2 || coverHeight < 80)
    return false;
  const int coverWidth = std::min(width - 12, coverHeight * 3 / 5);
  const Rect frame{safe.x + (safe.width - coverWidth) / 2, safe.y + margin, coverWidth, coverHeight};
  renderer.clearScreen();
  if (!BaseTheme::drawCoverThumbFill(renderer, cover, frame)) return false;
  renderer.drawRect(frame.x, frame.y, frame.width, frame.height);
  int y = frame.y + frame.height + 12;
  const int x = safe.x + margin;
  textInBox(renderer, UI_10_FONT_ID, x, y, width, title, true, EpdFontFamily::BOLD);
  y += line + 16;
  char value[32];
  if (data.progress >= 0)
    snprintf(value, sizeof(value), "%d%%", data.progress);
  else
    snprintf(value, sizeof(value), "-");
  textInBox(renderer, UI_12_FONT_ID, x, y, width, value, true, EpdFontFamily::BOLD);
  y += large + 6;
  textInBox(renderer, UI_10_FONT_ID, x, y, width, tr(STR_STATS_PROGRESS), true);
  y += line + 18;
  const int half = width / 2;
  BookReadingStats::formatDuration(data.book.totalReadingSeconds, value, sizeof(value));
  statCell(renderer, x, y, half, cellHeight, tr(STR_STATS_TOTAL_TIME), value);
  const uint32_t remaining =
      data.progress >= 0 && !data.book.isCompleted ? data.book.remainingReadingSeconds(data.progress * 100) : 0;
  if (remaining)
    BookReadingStats::formatDuration(remaining, value, sizeof(value));
  else
    snprintf(value, sizeof(value), "-");
  statCell(renderer, x + half, y, width - half, cellHeight, tr(STR_BAR_TIME_BOOK), value);
  return true;
}

bool drawMinimalBook(GfxRenderer& renderer, const char* title, int progress, uint32_t readingSeconds,
                     const Bitmap* cover) {
  const Rect safe = UITheme::getContentArea(renderer);
  const int margin = std::clamp(safe.width / 30, 8, 24);
  const int line = renderer.getLineHeight(UI_10_FONT_ID);
  const int available = safe.height - 2 * margin - 2 * line - 30;
  const int coverHeight = std::min({583, available, (safe.width - 2 * margin) * 5 / 3});
  const int coverWidth = coverHeight * 3 / 5;
  if (line <= 0 || coverHeight < 80 || coverWidth < 48) return false;
  const Rect frame{safe.x + (safe.width - coverWidth) / 2, safe.y + margin + (available - coverHeight) / 2, coverWidth,
                   coverHeight};
  renderer.clearScreen();
  if (!cover || !BaseTheme::drawCoverThumbFill(renderer, *cover, frame)) {
    renderer.clearScreen();
    renderer.drawRect(frame.x, frame.y, frame.width, frame.height);
    textInBox(renderer, UI_10_FONT_ID, frame.x + 8, frame.y + (frame.height - line) / 2, frame.width - 16, title, true,
              EpdFontFamily::BOLD);
  }
  const int durationY = frame.y + frame.height + 8;
  if (readingSeconds) {
    char duration[32];
    BookReadingStats::formatDuration(readingSeconds, duration, sizeof(duration));
    textInBox(renderer, UI_10_FONT_ID, frame.x, durationY, frame.width, duration);
  }
  if (progress >= 0) {
    const int percent = std::clamp(progress, 0, 100);
    const int barY = durationY + line + 4;
    renderer.drawRect(frame.x, barY, frame.width, 6);
    const int fill = (frame.width - 2) * percent / 100;
    if (fill > 0) renderer.fillRect(frame.x + 1, barY + 1, fill, 4);
    char label[8];
    snprintf(label, sizeof(label), "%d%%", percent);
    textInBox(renderer, UI_10_FONT_ID, frame.x, barY + 9, frame.width, label, true);
  }
  return true;
}

bool drawBookDashboard(GfxRenderer& renderer, const char* title, const char* author, const DashboardData& data,
                       const Bitmap* cover) {
  const Rect safe = UITheme::getContentArea(renderer);
  const int margin = std::clamp(safe.width / 30, 8, 24);
  const int line = renderer.getLineHeight(UI_10_FONT_ID);
  const int width = safe.width - 2 * margin;
  const int coverWidth = (width - 18) / 2;
  const int coverHeight = std::min({420, coverWidth * 5 / 3, safe.height - 2 * margin - 5 * line - 48});
  const int rows = data.hasStats ? 7 : 1;
  if (line <= 0 || coverWidth < 80 || coverHeight < rows * (2 * line + 4)) return false;
  const Rect frame{safe.x + margin, safe.y + margin, coverWidth, coverHeight};
  const int statsX = frame.x + frame.width + 18;
  const int statsWidth = safe.x + safe.width - margin - statsX;
  renderer.clearScreen();
  if (!cover || !BaseTheme::drawCoverThumbFill(renderer, *cover, frame)) {
    renderer.clearScreen();
    renderer.drawRect(frame.x, frame.y, frame.width, frame.height);
    textInBox(renderer, UI_10_FONT_ID, frame.x + 8, frame.y + (frame.height - line) / 2, frame.width - 16, title, true,
              EpdFontFamily::BOLD);
  }
  const int rowHeight = coverHeight / rows;
  const auto row = [&](int index, const char* label, const char* value) {
    const int y = frame.y + index * rowHeight;
    textInBox(renderer, UI_10_FONT_ID, statsX, y, statsWidth, value, false, EpdFontFamily::BOLD);
    textInBox(renderer, UI_10_FONT_ID, statsX, y + line + 3, statsWidth, label);
  };
  char value[32];
  if (data.progress >= 0)
    snprintf(value, sizeof(value), "%d%%", std::clamp(data.progress, 0, 100));
  else
    snprintf(value, sizeof(value), "-");
  row(0, tr(STR_STATS_PROGRESS), value);
  if (data.hasStats) {
    BookReadingStats::formatDuration(data.readingSeconds, value, sizeof(value));
    row(1, tr(STR_STATS_TOTAL_TIME), value);
    if (data.remainingSeconds)
      BookReadingStats::formatDuration(data.remainingSeconds, value, sizeof(value));
    else
      snprintf(value, sizeof(value), "-");
    row(2, tr(STR_BAR_TIME_BOOK), value);
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(data.sessions));
    row(3, tr(STR_STATS_SESSIONS), value);
    if (data.readingSeconds) {
      const uint64_t tenths = static_cast<uint64_t>(data.pages) * 600 / data.readingSeconds;
      snprintf(value, sizeof(value), "%llu.%llu", static_cast<unsigned long long>(tenths / 10),
               static_cast<unsigned long long>(tenths % 10));
    } else
      snprintf(value, sizeof(value), "-");
    row(4, tr(STR_STATS_PAGES_PER_MINUTE), value);
    dateValue(data.startDate, value, sizeof(value));
    row(5, tr(STR_STATS_STARTED), value);
    if (data.hasDailyAverage)
      BookReadingStats::formatDuration(data.dailyAverageSeconds, value, sizeof(value));
    else
      snprintf(value, sizeof(value), "-");
    row(6, tr(STR_STATS_DAILY_AVERAGE), value);
  }
  const int textY = frame.y + frame.height + 16;
  textInBox(renderer, UI_10_FONT_ID, frame.x, textY, width, title, false, EpdFontFamily::BOLD);
  if (author && *author) textInBox(renderer, UI_10_FONT_ID, frame.x, textY + line + 5, width, author);
  if (data.hasGlobal) {
    const int footerY = safe.y + safe.height - margin - 2 * line - 3;
    const int columns = data.hasStreak ? 3 : 2;
    const int cell = width / columns;
    BookReadingStats::formatDuration(data.globalSeconds, value, sizeof(value));
    textInBox(renderer, UI_10_FONT_ID, frame.x, footerY, cell, value, false, EpdFontFamily::BOLD);
    textInBox(renderer, UI_10_FONT_ID, frame.x, footerY + line + 3, cell, tr(STR_STATS_TOTAL_TIME));
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(data.completedBooks));
    textInBox(renderer, UI_10_FONT_ID, frame.x + cell, footerY, cell, value, false, EpdFontFamily::BOLD);
    textInBox(renderer, UI_10_FONT_ID, frame.x + cell, footerY + line + 3, cell, tr(STR_STATS_COMPLETED));
    if (data.hasStreak) {
      snprintf(value, sizeof(value), "%u", static_cast<unsigned>(data.currentStreak));
      textInBox(renderer, UI_10_FONT_ID, frame.x + 2 * cell, footerY, width - 2 * cell, value, false,
                EpdFontFamily::BOLD);
      textInBox(renderer, UI_10_FONT_ID, frame.x + 2 * cell, footerY + line + 3, width - 2 * cell,
                tr(STR_STATS_CURRENT_STREAK));
    }
  }
  return true;
}
}  // namespace sleepstats
