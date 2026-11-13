#include <EpdFont.h>
#include <builtinFonts/ubuntu_10_regular.h>
#include <builtinFonts/ubuntu_12_regular.h>
#include <components/bars/tab-bar.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "util/PluginPickerLayout.h"

namespace fui = freeink::ui;

namespace {
class FontTarget final : public fui::DrawTarget {
 public:
  fui::Size measureText(fui::FontId font, const char* text, fui::TextStyle) const override {
    int width = 0;
    int height = 0;
    (font == 0 ? small : body).getTextDimensions(text, &width, &height);
    return {static_cast<int16_t>(width), lineHeight(font)};
  }
  int16_t lineHeight(fui::FontId font) const override {
    return font == 0 ? ubuntu_10_regular.advanceY : ubuntu_12_regular.advanceY;
  }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  std::vector<std::pair<fui::Rect, std::string>> drawnText;
  void text(fui::Rect rect, const char* value, fui::TextStyle) override { drawnText.emplace_back(rect, value); }
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}

 private:
  EpdFont small{&ubuntu_10_regular};
  EpdFont body{&ubuntu_12_regular};
};

fui::ListProps pickerProps() {
  fui::ListProps props;
  props.labelText.font = 1;
  props.labelText.maxLines = 2;
  props.subtitleText.font = 0;
  props.subtitleText.maxLines = 2;
  props.valueText.font = 0;
  props.rowPaddingY = 8;
  props.rowHeight = 48;
  props.sidePadding = 16;
  return props;
}

std::string wrapped(const FontTarget& target, const char* text, const fui::ListProps& props, int width) {
  std::string result;
  fui::layoutText(target, {0, 0, static_cast<int16_t>(width), 1}, text, props.subtitleText,
                  [&result](const char* line, fui::Rect) {
                    if (!result.empty()) result += ' ';
                    result += line;
                  });
  return result;
}
}  // namespace

TEST(PluginPickerLayout, AllFiveEventsRemainReadableWithFirmwareFonts) {
  const FontTarget target;
  static constexpr const char* DISCLOSURES[] = {
      "Receives: book opens, book closes and progress, reading sessions, downloads, sleep and current book",
      "Recibe: apertura de libros, cierre y progreso, sesiones de lectura, descargas, reposo y libro actual",
      ("Rep: obertura de llibres, tancament i progr\xc3\xa9s, sessions de lectura, baixades, rep\xc3\xb2s i llibre "
       "actual")};
  for (const char* disclosure : DISCLOSURES) {
    for (const int width : {240, 384, 480, 800}) {
      auto props = pickerProps();
      const int contentWidth = width - props.sidePadding * 2;
      if (width <= 384) EXPECT_NE(wrapped(target, disclosure, props, contentWidth), disclosure);
      fitPluginSubtitleToViewport(target, width < 480 ? 320 : 200, props);
      EXPECT_EQ(wrapped(target, disclosure, props, contentWidth), disclosure) << width;
    }
  }
}

TEST(PluginPickerLayout, LongDescriptionsAndWrappedTitlesFitWithinViewport) {
  const FontTarget target;
  std::string description;
  for (int i = 0; i < 100; ++i) description += "Long plugin description. ";
  const fui::ListItem item{"A plugin with a long title that wraps over two lines", description.c_str(), ">"};
  for (const int height : {160, 200, 320, 640}) {
    for (const int width : {240, 384, 480, 800}) {
      auto props = pickerProps();
      fitPluginSubtitleToViewport(target, height, props);
      const auto layout = fui::measureListRow(target, nullptr, width, props, item);
      EXPECT_LE(layout.height, height) << width << "x" << height;
      EXPECT_LE(props.subtitleText.maxLines, 16);
    }
  }
}

TEST(PluginPickerLayout, ShortSubtitlesKeepTheirOriginalRowHeight) {
  const FontTarget target;
  const fui::ListItem item{"Plugin", "One event", ">"};
  auto props = pickerProps();
  const auto before = fui::measureListRow(target, nullptr, 480, props, item);
  fitPluginSubtitleToViewport(target, 640, props);
  const auto after = fui::measureListRow(target, nullptr, 480, props, item);
  EXPECT_EQ(after.height, before.height);
}
TEST(LibraryMetadataTabs, AllFiveLabelsAndTouchTargetsFitReaderOrientations) {
  const char* labels[] = {"Recent", "Title", "Author", "Series", "Genre"};
  struct Theme {
    int font;
    int gap;
    bool fullSlot;
  };
  // Production Base, Lyra and RoundedRaff properties, plus a wider-gap stress case.
  const Theme themes[] = {{0, 10, false}, {0, 8, false}, {1, 0, true}, {1, 10, true}};
  for (const char* recentLabel : {"Recent", "Opened"}) {
    labels[0] = recentLabel;
    for (const char* authorLabel : {"Author", "First name"}) {
      labels[2] = authorLabel;
      for (const auto size : {fui::Size{480, 800}, fui::Size{800, 480}}) {
        for (const Theme theme : themes) {
          for (const bool focused : {false, true}) {
            for (int selected = 0; selected < 5; ++selected) {
              FontTarget target;
              fui::TabItem tabs[5];
              for (int i = 0; i < 5; ++i) {
                tabs[i] = fui::tabItem(i, i == selected, true, labels[i]);
                if (i == selected) tabs[i].indicator = fui::TabIndicator::Down;
              }
              fui::TabBarProps props;
              props.tabs = tabs;
              props.count = 5;
              props.layout = fui::TabBarLayout::ContentWidth;
              props.action = 1;
              props.inputMask = fui::InputTouch;
              props.text.font = theme.font;
              props.text.maxLines = 1;
              props.contentInset = {2, 0, 2, 0};
              props.gap = theme.gap;
              props.tabInset = theme.fullSlot ? fui::Insets{4, 4, 7, 4}
                               : focused      ? fui::Insets{2, 4, 4, 4}
                                              : fui::Insets{2, 4, 0, 4};
              if (!focused && !theme.fullSlot) props.selectedUnderline = 2;
              const int height = std::max(50, target.lineHeight(theme.font) + 10);
              fui::DeviceContext device;
              device.width = size.width;
              device.height = size.height;
              fui::InteractionBuffer<8> interactions;
              fui::Frame<8> frame(target, device, {}, interactions);
              fui::tabBar(frame, {20, 0, static_cast<int16_t>(size.width - 40), static_cast<int16_t>(height - 1)},
                          props);
              ASSERT_EQ(target.drawnText.size(), 5u);
              EXPECT_EQ(interactions.count(), 5u);
              for (size_t i = 0; i < 5; ++i) {
                EXPECT_EQ(target.drawnText[i].second, labels[i]);
                EXPECT_GE(target.drawnText[i].first.x, 20);
                EXPECT_LE(target.drawnText[i].first.right(), size.width - 20);
                EXPECT_LE(target.drawnText[i].first.bottom(), height);
              }
            }
          }
        }
      }
    }
  }
}
