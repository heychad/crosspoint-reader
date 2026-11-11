#include "ReadingChartsActivity.h"

#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "activities/reader/BookReadingStats.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;
namespace {
constexpr StrId VIEW_LABELS[] = {StrId::STR_STATS_TIME_OF_DAY, StrId::STR_STATS_WEEKDAYS, StrId::STR_STATS_CALENDAR};
constexpr StrId TIME_LABELS[] = {StrId::STR_STATS_MORNING, StrId::STR_STATS_AFTERNOON, StrId::STR_STATS_EVENING,
                                 StrId::STR_STATS_NIGHT};
constexpr StrId DAY_LABELS[] = {StrId::STR_STATS_MONDAY,   StrId::STR_STATS_TUESDAY, StrId::STR_STATS_WEDNESDAY,
                                StrId::STR_STATS_THURSDAY, StrId::STR_STATS_FRIDAY,  StrId::STR_STATS_SATURDAY,
                                StrId::STR_STATS_SUNDAY};
constexpr StrId SHORT_DAYS[] = {StrId::STR_STATS_MON_SHORT, StrId::STR_STATS_TUE_SHORT, StrId::STR_STATS_WED_SHORT,
                                StrId::STR_STATS_THU_SHORT, StrId::STR_STATS_FRI_SHORT, StrId::STR_STATS_SAT_SHORT,
                                StrId::STR_STATS_SUN_SHORT};
constexpr StrId MONTHS[] = {StrId::STR_MONTH_JAN, StrId::STR_MONTH_FEB, StrId::STR_MONTH_MAR, StrId::STR_MONTH_APR,
                            StrId::STR_MONTH_MAY, StrId::STR_MONTH_JUN, StrId::STR_MONTH_JUL, StrId::STR_MONTH_AUG,
                            StrId::STR_MONTH_SEP, StrId::STR_MONTH_OCT, StrId::STR_MONTH_NOV, StrId::STR_MONTH_DEC};
}  // namespace

ReadingChartsActivity::ReadingChartsActivity(GfxRenderer& renderer, MappedInputManager& input,
                                             const readingcharts::Data& snapshot, const char* caption)
    : Activity("ReadingCharts", renderer, input),
      UiAppHost(renderer),
      data(snapshot),
      months(readingcharts::monthRange(data)),
      month(months.last) {
  if (!caption) return;
  size_t length = std::min(strlen(caption), sizeof(title) - 1);
  while (length && (static_cast<unsigned char>(caption[length]) & 0xc0) == 0x80) --length;
  memcpy(title, caption, length);
}

void ReadingChartsActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.setScreen(&ReadingChartsActivity::buildScreen, this);
  for (fui::ActionId action = ACTION_TAB; action <= ACTION_BACK; ++action)
    app.on(action, &ReadingChartsActivity::onAction, this);
  requestUpdate();
}

void ReadingChartsActivity::onExit() {
  closeRouting();
  Activity::onExit();
}

void ReadingChartsActivity::changeView(const int selected) {
  if (selected < 0 || selected >= viewCount()) return;
  RenderLock lock(*this);
  closeRouting();
  view = static_cast<View>(selected);
  app.clearTapFlash();
  requestUpdate();
}

void ReadingChartsActivity::step(const int direction) {
  if (view != View::Calendar) {
    changeView((static_cast<int>(view) + direction + viewCount()) % viewCount());
    return;
  }
  RenderLock lock(*this);
  if (months.first < 0) return;
  const int next = std::clamp(month + direction, months.first, months.last);
  if (next == month) return;
  closeRouting();
  month = next;
  app.clearTapFlash();
  requestUpdate();
}

void ReadingChartsActivity::onAction(const fui::ActionEvent& event, void* context) {
  auto& self = *static_cast<ReadingChartsActivity*>(context);
  switch (event.action) {
    case ACTION_TAB:
      self.changeView(event.value);
      break;
    case ACTION_PREVIOUS:
      self.step(-1);
      break;
    case ACTION_NEXT:
      self.step(1);
      break;
    case ACTION_BACK:
      self.finish();
      break;
  }
}

bool ReadingChartsActivity::handleHomeGesture() {
  finish();
  return true;
}

void ReadingChartsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (!routingReady()) return;
  if (routeTouch(mappedInput).event) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm))
    changeView((static_cast<int>(view) + 1) % viewCount());
  else if (mappedInput.wasReleased(MappedInputManager::Button::NavNext))
    step(1);
  else if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious))
    step(-1);
}

void ReadingChartsActivity::buildScreen(UiScreen& screen, void* context) {
  static_cast<ReadingChartsActivity*>(context)->build(screen);
}

void ReadingChartsActivity::build(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width + metrics.contentSidePadding),
                  static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x + metrics.contentSidePadding)});
  screen.spacer(metrics.verticalSpacing);
  const int lineHeight = screen.target().lineHeight(screen.theme().smallText.font);
  for (int i = 0; i < viewCount(); ++i) {
    tabs[i].label = I18N.get(VIEW_LABELS[i]);
    tabs[i].value = i;
    tabs[i].selected = i == static_cast<int>(view);
  }
  tabProps.tabs = tabs.data();
  tabProps.count = viewCount();
  tabProps.action = ACTION_TAB;
  tabProps.inputMask = fui::InputTouch;
  tabProps.text = screen.theme().smallText;
  tabProps.tabInset = fui::Insets{2, 2, 2, 2};
  tabProps.contentInset = fui::Insets{4, 4, 4, 4};
  fui::tabBar(screen.frame(), screen.takeTop(std::max<int>(screen.theme().minTouchSize, lineHeight + 12)), tabProps);
  if (data.warning != readingcharts::Warning::None) {
    screen.target().text(screen.takeTop(lineHeight),
                         data.warning == readingcharts::Warning::UnsavedPartial ? tr(STR_STATS_SAVE_PARTIAL)
                         : data.warning == readingcharts::Warning::Unsaved      ? tr(STR_STATS_SAVE_INCOMPLETE)
                                                                                : tr(STR_STATS_PARTIAL),
                         screen.theme().smallText);
  }
  if (mappedInput.hasTouch()) {
    const auto controls = screen.takeBottom(std::max<int>(screen.theme().minTouchSize, metrics.menuRowHeight));
    button.text = screen.theme().smallText;
    button.styles = screen.theme().button;
    button.inputMask = fui::InputTouch;
    button.minTouchSize = screen.theme().minTouchSize;
    const int width = controls.width / 3;
    for (int i = 0; i < 3; ++i) {
      button.label = i == 1                   ? tr(STR_BACK)
                     : view == View::Calendar ? (i == 0 ? tr(STR_STATS_PREV_MONTH) : tr(STR_STATS_NEXT_MONTH))
                                              : (i == 0 ? tr(STR_STATS_PREV_VIEW) : tr(STR_STATS_NEXT_VIEW));
      button.action = i == 0 ? ACTION_PREVIOUS : i == 1 ? ACTION_BACK : ACTION_NEXT;
      button.enabled = view != View::Calendar || i == 1 || (i == 0 ? month > months.first : month < months.last);
      fui::button(screen.frame(),
                  fui::Rect{static_cast<int16_t>(controls.x + i * width), controls.y, static_cast<int16_t>(width),
                            controls.height},
                  button);
    }
    screen.spacer(metrics.verticalSpacing, fui::LayoutAnchor::Bottom);
  }
  if (!data.available) {
    screen.centeredText(tr(STR_STATS_DATA_UNAVAILABLE));
    return;
  }
  if (view == View::Calendar)
    drawCalendar(screen, screen.body());
  else
    drawBars(screen, screen.body());
}

void ReadingChartsActivity::drawBars(UiScreen& screen, const fui::Rect area) {
  const bool time = view == View::TimeOfDay;
  const auto* values = time ? data.timeOfDay.data() : data.weekdays.data();
  const auto* labels = time ? TIME_LABELS : DAY_LABELS;
  const int count = time ? data.timeOfDay.size() : data.weekdays.size();
  const uint32_t maximum = *std::max_element(values, values + count);
  if (maximum == 0) {
    screen.centeredText(tr(STR_STATS_NO_CHART_DATA));
    return;
  }
  const int lineHeight = screen.target().lineHeight(screen.theme().smallText.font);
  const int columns = area.height / count < lineHeight + 5 && area.width >= 400 ? 2 : 1;
  const int rows = (count + columns - 1) / columns;
  const int rowHeight = area.height / rows;
  const int columnWidth = (area.width - (columns - 1) * 12) / columns;
  if (rowHeight < lineHeight + 5) {
    screen.centeredText(tr(STR_STATS_CHART_SPACE));
    return;
  }
  auto numberStyle = screen.theme().smallText;
  numberStyle.align = fui::TextAlign::Right;
  const int valueWidth = columnWidth / 3;
  for (int i = 0; i < count; ++i) {
    const int x = area.x + (i / rows) * (columnWidth + 12);
    const int y = area.y + (i % rows) * rowHeight;
    screen.target().text(
        fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(columnWidth - valueWidth - 8),
                  static_cast<int16_t>(lineHeight)},
        I18N.get(labels[i]), screen.theme().smallText);
    BookReadingStats::formatDuration(values[i], text, sizeof(text));
    screen.target().text(fui::Rect{static_cast<int16_t>(x + columnWidth - valueWidth), static_cast<int16_t>(y),
                                   static_cast<int16_t>(valueWidth), static_cast<int16_t>(lineHeight)},
                         text, numberStyle);
    const int width = readingcharts::barWidth(values[i], maximum, columnWidth);
    screen.target().fill(
        fui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y + lineHeight + 2), static_cast<int16_t>(width), 3},
        fui::Paint::solid(fui::Color::Black));
  }
}

void ReadingChartsActivity::drawCalendar(UiScreen& screen, fui::Rect area) {
  const auto date = readingcharts::monthDate(month);
  if (!date.isValid()) {
    screen.centeredText(tr(STR_STATS_NO_CHART_DATA));
    return;
  }
  const int lineHeight = screen.target().lineHeight(screen.theme().smallText.font);
  auto centered = screen.theme().smallText;
  centered.align = fui::TextAlign::Center;
  const bool wide = area.width > area.height * 2;
  fui::Rect grid = area;
  fui::Rect legend = area;
  if (wide) {
    grid.width = area.width * 2 / 3;
    legend.x = grid.right() + 8;
    legend.width = area.right() - legend.x;
  } else {
    grid.y += lineHeight + 4;
    grid.height -= 3 * lineHeight + 12;
    legend.y = grid.bottom() + 4;
    legend.height = 2 * lineHeight;
  }
  const fui::Rect monthLabel{wide ? legend.x : area.x, wide ? legend.y : area.y, wide ? legend.width : area.width,
                             static_cast<int16_t>(lineHeight)};
  snprintf(text, sizeof(text), "%s %u", I18N.get(MONTHS[date.month - 1]), static_cast<unsigned>(date.year));
  screen.target().text(monthLabel, text, centered);
  if (wide) {
    legend.y += lineHeight + 4;
    legend.height -= lineHeight + 4;
  }
  const int cellWidth = grid.width / 7;
  const int cellHeight = (grid.height - lineHeight) / 6;
  if (cellHeight < lineHeight || cellWidth < lineHeight) {
    screen.centeredText(tr(STR_STATS_CHART_SPACE));
    return;
  }
  for (int i = 0; i < 7; ++i)
    screen.target().text(fui::Rect{static_cast<int16_t>(grid.x + i * cellWidth), grid.y,
                                   static_cast<int16_t>(cellWidth), static_cast<int16_t>(lineHeight)},
                         I18N.get(SHORT_DAYS[i]), centered);
  const int offset = readingStatsDayOfWeekIndex(date);
  unsigned recorded = 0;
  for (int day = 1; day <= daysInMonth(date.year, date.month); ++day) {
    const auto state = readingcharts::dayState(data, {date.year, date.month, static_cast<uint8_t>(day)});
    const int index = offset + day - 1;
    const fui::Rect box{static_cast<int16_t>(grid.x + (index % 7) * cellWidth + 1),
                        static_cast<int16_t>(grid.y + lineHeight + (index / 7) * cellHeight),
                        static_cast<int16_t>(cellWidth - 2), static_cast<int16_t>(cellHeight)};
    auto style = centered;
    if (state == readingcharts::Day::Recorded) {
      ++recorded;
      screen.target().fill(box, fui::Paint::solid(fui::Color::Black));
      style.color = fui::Color::White;
      style.inverted = true;
    } else if (state == readingcharts::Day::OutsideHistory) {
      screen.target().fill(box, fui::Paint::dither(fui::Color::LightGray));
    }
    snprintf(text, sizeof(text), "%d", day);
    screen.target().text(box, text, style);
  }
  snprintf(text, sizeof(text), tr(STR_STATS_RECORDED_DAYS), recorded);
  screen.target().text(fui::Rect{legend.x, legend.y, legend.width, static_cast<int16_t>(lineHeight)}, text, centered);
  const int half = wide ? legend.width : legend.width / 2;
  const int legendY = legend.y + lineHeight;
  const fui::Rect recordedMark{legend.x, static_cast<int16_t>(legendY + 4), 10, 10};
  const fui::Rect outside{static_cast<int16_t>(wide ? legend.x : legend.x + half),
                          static_cast<int16_t>(wide ? recordedMark.y + lineHeight : recordedMark.y), 10, 10};
  screen.target().fill(recordedMark, fui::Paint::solid(fui::Color::Black));
  screen.target().fill(outside, fui::Paint::dither(fui::Color::LightGray));
  screen.target().text(fui::Rect{static_cast<int16_t>(recordedMark.x + 14), static_cast<int16_t>(recordedMark.y - 4),
                                 static_cast<int16_t>(half - 14), static_cast<int16_t>(lineHeight)},
                       tr(STR_STATS_RECORDED), screen.theme().smallText);
  screen.target().text(fui::Rect{static_cast<int16_t>(outside.x + 14), static_cast<int16_t>(outside.y - 4),
                                 static_cast<int16_t>(half - 14), static_cast<int16_t>(lineHeight)},
                       tr(STR_STATS_OUTSIDE_HISTORY), screen.theme().smallText);
}

void ReadingChartsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  GUI.drawHeader(renderer, UITheme::getInstance().getHeaderRect(renderer, true, false),
                 title[0] ? title : tr(STR_STATS_CHARTS));
  const bool calendar = view == View::Calendar;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_STATS_NEXT_VIEW),
                                            calendar ? tr(STR_STATS_PREV_MONTH) : tr(STR_STATS_PREV_VIEW),
                                            calendar ? tr(STR_STATS_NEXT_MONTH) : tr(STR_STATS_NEXT_VIEW));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
