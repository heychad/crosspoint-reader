#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <FreeInkApp.h>
#include <I18nKeys.h>
#include <Utf8.h>
#include <builtinFonts/ubuntu_10_regular.h>
#include <builtinFonts/ubuntu_12_regular.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "activities/reader/ReadingChartModel.h"
using enum StrId;
namespace BidiUtils {
bool applyBidiVisual(const char*, std::string&) { return false; }
}  // namespace BidiUtils
namespace fui = freeink::ui;
#define LOG_ERR(...) ((void)0)
// TRANSLATIONS
struct {
  const char* get(StrId id) const { return tr(id); }
} I18N;
struct Rect {
  int x, y, width, height;
};
Rect safe{8, 8, 464, 744};
int activePane = 0;
bool noSpace = false;
struct Metrics {
  int topPadding = 8, headerHeight = 48, tabBarHeight = 48, verticalSpacing = 8, contentSidePadding = 12,
      menuRowHeight = 48, buttonHintsHeight = 32;
} metrics;
// METRICS
class Target final : public fui::DrawTarget {
 public:
  fui::Size measureText(fui::FontId id, const char* text, fui::TextStyle) const override {
    int w = 0, h = 0;
    (id == 0 ? small : body).getTextDimensions(text, &w, &h);
    return {static_cast<int16_t>(w), lineHeight(id)};
  }
  int16_t lineHeight(fui::FontId id) const override {
    return id == 0 ? ubuntu_10_regular.advanceY : ubuntu_12_regular.advanceY;
  }
  void fill(fui::Rect r, fui::Paint, uint8_t, uint8_t) override { bounds(r); }
  void stroke(fui::Rect r, fui::Paint, uint8_t, uint8_t, uint8_t) override { bounds(r); }
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}
  void text(fui::Rect r, const char* text, fui::TextStyle style) override {
    bounds(r);
    if (std::string(text) == "More display space needed") noSpace = true;
    if (r.height < lineHeight(style.font))
      fprintf(stderr, "input pane=%d text=%s font=%d screen=%dx%d rect=%d,%d %dx%d\n", activePane, text, style.font,
              safe.width, safe.height, r.x, r.y, r.width, r.height);
    fui::layoutText(*this, r, text, style, [r](const char*, fui::Rect line) {
      if (line.y < r.y || line.bottom() > r.bottom())
        fprintf(stderr, "text pane=%d line=%d,%d %dx%d rect=%d,%d %dx%d\n", activePane, line.x, line.y, line.width,
                line.height, r.x, r.y, r.width, r.height);
      assert(line.x >= r.x && line.right() <= r.right());
      assert(line.y >= r.y && line.bottom() <= r.bottom());
    });
  }
  static void bounds(fui::Rect r) {
    if (r.x < safe.x || r.right() > safe.x + safe.width || r.y < safe.y || r.bottom() > safe.y + safe.height)
      fprintf(stderr, "pane=%d rect=%d,%d %dx%d safe=%d,%d %dx%d\n", activePane, r.x, r.y, r.width, r.height, safe.x,
              safe.y, safe.width, safe.height);
    assert(r.x >= safe.x && r.right() <= safe.x + safe.width);
    assert(r.y >= safe.y && r.bottom() <= safe.y + safe.height);
  }

 private:
  EpdFont small{&ubuntu_10_regular}, body{&ubuntu_12_regular};
};
struct GfxRenderer {
  Target target;
  int width = 480, height = 800;
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  int getLineHeight(int font) const { return target.lineHeight(font); }
  int getTextWidth(int font, const char* text) const { return target.measureText(font, text, {}).width; }
  int getTextAdvanceX(int font, const char* text, EpdFontFamily::Style) const { return getTextWidth(font, text); }
  void prewarmFallbackText(int, const char*, EpdFontFamily::Style, bool) {}
  void prewarmFallbackText(int, const char* (*)(const void*, uint32_t), const void*, size_t, EpdFontFamily::Style,
                           bool) {}
  void drawText(int font, int x, int y, const char* text) {
    const int w = getTextWidth(font, text);
    assert(x >= safe.x && x + w <= safe.x + safe.width);
    assert(y >= safe.y && y + getLineHeight(font) <= safe.y + safe.height);
  }
};

struct UITheme {
  static UITheme& getInstance() {
    static UITheme theme;
    return theme;
  }
  const Metrics& getMetrics() const { return metrics; }
  Rect getScreenSafeArea(const GfxRenderer&, bool, bool) const { return safe; }
};
struct Input {
  bool touch = true;
  bool hasTouch() const { return touch; }
};
struct BookReadingStats {
  static void formatDuration(uint32_t, char*, size_t);
};
// DURATION
// LABELS
struct ReadingChartsActivity {
  using UiScreen = fui::Screen<24>;
  enum class View : uint8_t { TimeOfDay, Weekdays, Calendar };
  static constexpr fui::ActionId ACTION_TAB = 1, ACTION_PREVIOUS = 2, ACTION_NEXT = 3, ACTION_BACK = 4;
  readingcharts::Data data;
  readingcharts::MonthRange months;
  View view = View::TimeOfDay;
  int month = 0;
  std::array<fui::TabItem, 3> tabs{};
  fui::TabBarProps tabProps;
  fui::ButtonProps button;
  char title[160] = "Test book or device";
  char text[80]{};
  GfxRenderer renderer;
  Input mappedInput;
  int viewCount() const { return data.scope == readingcharts::Scope::Book ? 2 : 3; }
  void build(UiScreen&);
  void drawBars(UiScreen&, fui::Rect);
  void drawCalendar(UiScreen&, fui::Rect);
};
// METHODS
int main() {
  const std::array<fui::Insets, 4> insets{{{9, 7, 3, 7}, {7, 9, 7, 3}, {3, 7, 9, 7}, {7, 3, 7, 9}}};
  for (const auto& profile : profiles) {
    for (int orientation = 0; orientation < 4; ++orientation) {
      const fui::Size size = orientation % 2 == 0 ? fui::Size{480, 800} : fui::Size{800, 480};
      for (bool touch : {false, true}) {
        metrics = profile;
        if (touch) metrics.buttonHintsHeight = 0;
        for (int font : {0, 1}) {
          for (auto warning : {readingcharts::Warning::None, readingcharts::Warning::Partial,
                               readingcharts::Warning::Unsaved, readingcharts::Warning::UnsavedPartial}) {
            for (int selected = 0; selected < 3; ++selected) {
              ReadingChartsActivity activity;
              activity.renderer.width = size.width;
              activity.renderer.height = size.height;
              activity.mappedInput.touch = touch;
              activity.data.available = true;
              activity.data.scope = readingcharts::Scope::Device;
              activity.data.warning = warning;
              activity.data.timeOfDay = {3600, 1800, 600, 60};
              activity.data.weekdays = {900, 1800, 10, 80, 3600, 120, 60};
              activity.data.anchor = readingStatsDayIndex({2026, 11, 4});
              activity.data.history.fill(0x55);
              activity.data.today = {2026, 11, 4};
              activity.months = readingcharts::monthRange(activity.data);
              activity.month = activity.months.last;
              activity.view = static_cast<ReadingChartsActivity::View>(selected);
              activePane = selected;
              const auto inset = insets[orientation];
              safe = {inset.left, inset.top, size.width - inset.left - inset.right,
                      size.height - inset.top - inset.bottom};
              if (!touch) {
                if (orientation == 0) safe.height -= metrics.buttonHintsHeight;
                if (orientation == 1) {
                  safe.x += metrics.buttonHintsHeight;
                  safe.width -= metrics.buttonHintsHeight;
                }
                if (orientation == 2) {
                  safe.y += metrics.buttonHintsHeight;
                  safe.height -= metrics.buttonHintsHeight;
                }
                if (orientation == 3) safe.width -= metrics.buttonHintsHeight;
              }
              fui::DeviceContext device;
              device.width = size.width;
              device.height = size.height;
              device.hasTouch = touch;
              device.safeArea = inset;
              auto theme = fui::themeTokensForLineHeight(activity.renderer.target.lineHeight(font));
              theme.smallText.font = font;
              theme.bodyText.font = font;
              fui::InteractionBuffer<24> hits;
              fui::InputSnapshot input;
              fui::Frame<24> frame(activity.renderer.target, device, input, hits);
              fui::Screen<24> screen(frame, theme);
              noSpace = false;
              activity.build(screen);
              if (noSpace)
                fprintf(stderr, "space failed %dx%d font%d view%d warning%d touch%d\n", size.width, size.height, font,
                        selected, int(warning), touch);
              assert(!noSpace);
              assert(hits.count() <= 6);
              for (size_t i = 0; i < hits.count(); ++i) {
                const auto& hit = hits.data()[i];
                assert(hit.rect.x >= 0 && hit.rect.y >= 0 && hit.rect.right() <= size.width &&
                       hit.rect.bottom() <= size.height);
              }
            }
          }
        }
      }
    }
  }
}
