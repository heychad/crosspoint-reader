#include "ReaderBarRenderer.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalPowerManager.h>
#include <Utf8.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Only the RenderLock-protected render task uses this fixed scratch space.
struct BarScratch {
  std::array<std::array<char, 32>, ReaderBar::SLOT_COUNT> buffers{};
  std::array<const char*, ReaderBar::SLOT_COUNT> text{};
  std::array<int, ReaderBar::SLOT_COUNT> widths{};
  char clipped[192]{};
};
BarScratch scratch;

const char* clipText(const GfxRenderer& renderer, const char* text, int width) {
  if (width <= 0 || renderer.getTextWidth(SMALL_FONT_ID, "...") > width) return "";
  const size_t length = strnlen(text, sizeof(scratch.clipped) - 4);
  size_t low = 0, high = length, best = 0;
  while (low < high) {
    const size_t middle = low + (high - low + 1) / 2;
    const size_t cut = utf8SafeTruncateBuffer(text, middle);
    memcpy(scratch.clipped, text, cut);
    memcpy(scratch.clipped + cut, "...", 4);
    if (renderer.getTextWidth(SMALL_FONT_ID, scratch.clipped) <= width) {
      best = cut;
      low = middle;
    } else {
      high = middle - 1;
    }
  }
  memcpy(scratch.clipped, text, best);
  memcpy(scratch.clipped + best, "...", 4);
  return scratch.clipped;
}
}  // namespace

int readerbars::height(const ReaderBar& bar, bool forceText) {
  if (bar.hidden) return 0;
  const auto& metrics = UITheme::getInstance().getMetrics();
  return bar.height(metrics.statusBarVerticalMargin, metrics.progressBarMarginTop) +
         (forceText && !bar.hasText() ? metrics.statusBarVerticalMargin : 0);
}

void readerbars::draw(GfxRenderer& renderer, const ReaderBar& bar, ReaderBarPosition position, float bookPercent,
                      int page, int pages, bool estimated, const char* bookTitle, const char* chapterTitle,
                      bool bookmarked, const char* overrideTitle, const char* bookTime, const char* sectionTime,
                      const char* chapterTime) {
#if !defined(CROSSPOINT_EXPERIMENTAL_CHAPTER_TIME)
  (void)chapterTime;
#endif
  const auto& metrics = UITheme::getInstance().getMetrics();
  int mt, mr, mb, ml;
  renderer.getOrientedViewableTRBL(&mt, &mr, &mb, &ml);
  const bool top = position == ReaderBarPosition::Top;
  const int textHeight = bar.hasText() || overrideTitle ? metrics.statusBarVerticalMargin : 0;
  const int totalHeight = height(bar, overrideTitle != nullptr);
  if (totalHeight == 0) return;
  const int origin = top ? std::max(0, mt - 5) : renderer.getScreenHeight() - mb - totalHeight;
  const int y = origin + std::max(0, (textHeight - renderer.getLineHeight(SMALL_FONT_ID)) / 2);
  const int corner = top ? BoardConfig::ACTIVE.cornerRadius
                         : std::max(BoardConfig::ACTIVE.cornerRadius, BoardConfig::ACTIVE.bottomCornerRadius);
  const int left = std::max(ml, corner) + 1;
  const int right = renderer.getScreenWidth() - std::max(mr, corner);
  if (right <= left) return;

  scratch = {};
  auto& buffers = scratch.buffers;
  auto& text = scratch.text;
  auto& widths = scratch.widths;
  const bool batteryIcon = bar.batteryStyle != ReaderBarBatteryStyle::PercentOnly;
  const bool batteryPercent = bar.batteryStyle == ReaderBarBatteryStyle::IconAndPercent ||
                              bar.batteryStyle == ReaderBarBatteryStyle::PercentOnly ||
                              (bar.batteryStyle == ReaderBarBatteryStyle::FollowGlobal &&
                               SETTINGS.hideBatteryPercentage == CrossPointSettings::HIDE_NEVER);
  for (unsigned i = 0; i < ReaderBar::SLOT_COUNT; ++i) {
    auto& buf = buffers[i];
    text[i] = buf.data();
    switch (bar.slots[i]) {
      case ReaderBarItem::Empty:
        break;
      case ReaderBarItem::Clock:
        halClock.formatTime(buf.data(), buf.size(), SETTINGS.clockFormat == 1);
        break;
      case ReaderBarItem::Date: {
        struct tm now{};
        if (halClock.localTime(now)) strftime(buf.data(), buf.size(), "%Y-%m-%d", &now);
        break;
      }
      case ReaderBarItem::Battery:
        if (batteryPercent) snprintf(buf.data(), buf.size(), "%u%%", powerManager.getBatteryPercentage());
        break;
      case ReaderBarItem::ChapterPages:
        snprintf(buf.data(), buf.size(), "%s%d/%d", estimated ? "~" : "", page, pages);
        break;
      case ReaderBarItem::BookPercent:
        snprintf(buf.data(), buf.size(), "%.*f%%", bar.percentageDecimals, std::clamp(bookPercent, 0.0f, 100.0f));
        break;
      case ReaderBarItem::ChapterPercent:
        if (pages > 0)
          snprintf(buf.data(), buf.size(), "%.*f%%", bar.percentageDecimals,
                   std::clamp(100.0 * page / pages, 0.0, 100.0));
        else
          std::strcpy(buf.data(), "-");
        break;
      case ReaderBarItem::BookTitle:
        text[i] = bookTitle ? bookTitle : "";
        break;
      case ReaderBarItem::ChapterTitle:
        text[i] = chapterTitle ? chapterTitle : "";
        break;
      case ReaderBarItem::TimeLeftBook:
        text[i] = bookTime ? bookTime : "-";
        break;
      case ReaderBarItem::TimeLeftSection:
        text[i] = sectionTime ? sectionTime : "-";
        break;
#if defined(CROSSPOINT_EXPERIMENTAL_CHAPTER_TIME)
      case ReaderBarItem::TimeLeftChapter:
        text[i] = chapterTime ? chapterTime : "-";
        break;
#endif
      case ReaderBarItem::Count:
        break;
    }
    widths[i] = renderer.getTextWidth(SMALL_FONT_ID, text[i]);
    if (bar.slots[i] == ReaderBarItem::Battery && batteryIcon)
      widths[i] += metrics.batteryWidth + (batteryPercent ? 4 : 0);
  }
  if (overrideTitle) {
    text[ReaderBar::CENTER] = overrideTitle;
    widths[ReaderBar::CENTER] = renderer.getTextWidth(SMALL_FONT_ID, overrideTitle);
  }
  const int bookmarkReserve = bookmarked && !top && textHeight ? 12 : 0;
  const char* separator = readerBarSeparatorGlyph(bar.separator);
  auto layout = bar.centered ? layoutCenteredReaderBar(widths, left, right, renderer.getScreenWidth() / 2,
                                                       renderer.getTextWidth(SMALL_FONT_ID, separator), bookmarkReserve)
                             : layoutReaderBar(widths, left, right, bookmarkReserve);
  if (bar.centered && batteryIcon) {
    bool omitted = false;
    for (unsigned i = 0; i < widths.size(); ++i) {
      if (bar.slots[i] == ReaderBarItem::Battery && !(overrideTitle && i == ReaderBar::CENTER) && widths[i] > 0 &&
          widths[i] < metrics.batteryWidth) {
        widths[i] = 0;
        omitted = true;
      }
    }
    if (omitted)
      layout = layoutCenteredReaderBar(widths, left, right, renderer.getScreenWidth() / 2,
                                       renderer.getTextWidth(SMALL_FONT_ID, separator), bookmarkReserve);
  }
  if (bookmarkReserve) {
    // A compact ribbon fits the reserved space without a second bitmap buffer.
    renderer.drawRect(left, y + 2, 8, 10);
    renderer.drawLine(left, y + 12, left + 4, y + 9);
    renderer.drawLine(left + 4, y + 9, left + 7, y + 12);
  }
  for (unsigned i = 0; i < layout.separatorCount; ++i)
    renderer.drawText(SMALL_FONT_ID, layout.separatorX[i], y, separator);
  for (unsigned i = 0; i < ReaderBar::SLOT_COUNT; ++i) {
    int available = widths[i];
    int x = layout.x[i];
    if (!bar.centered && i == ReaderBar::CENTER) {
      available = layout.centerRight - layout.centerLeft;
      x = layout.centerLeft;
    }
    if (available <= 0) continue;
    if (bar.slots[i] == ReaderBarItem::Battery && batteryIcon && !(overrideTitle && i == ReaderBar::CENTER)) {
      // Never let a clipped battery overlap a neighboring slot.
      if (available >= metrics.batteryWidth) {
        const int fullWidth =
            metrics.batteryWidth + (batteryPercent ? 4 + renderer.getTextWidth(SMALL_FONT_ID, text[i]) : 0);
        const bool showPercent = batteryPercent && available >= fullWidth;
        const int drawnWidth = showPercent ? fullWidth : metrics.batteryWidth;
        const int batteryX = x + (!bar.centered && i == ReaderBar::CENTER ? (available - drawnWidth) / 2 : 0);
        GUI.drawBatteryLeft(renderer, Rect{batteryX, y, metrics.batteryWidth, metrics.batteryHeight}, showPercent);
      }
      continue;
    }
    if (!text[i] || !*text[i]) continue;
    const int natural = renderer.getTextWidth(SMALL_FONT_ID, text[i]);
    if (natural <= available) {
      renderer.drawText(SMALL_FONT_ID, x + (!bar.centered && i == ReaderBar::CENTER ? (available - natural) / 2 : 0), y,
                        text[i]);
    } else {
      renderer.drawText(SMALL_FONT_ID, x, y, clipText(renderer, text[i], available));
    }
  }
  if (bar.progress < 2) {
    const int thick = (bar.thickness + 1) * 2;
    const float fraction = bar.progress == 0 ? bookPercent / 100.0f : pages > 0 ? float(page) / pages : 0.0f;
    const int width = int(renderer.getScreenWidth() * std::clamp(fraction, 0.0f, 1.0f));
    if (width > 0) renderer.fillRect(0, origin + totalHeight - thick, width, thick, true);
  }
}
