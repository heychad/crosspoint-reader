#include "ReaderBarsSettingsActivity.h"

#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <utility>

#include "CrossPointSettings.h"
#include "EnumSelectionActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;
namespace {
constexpr StrId SLOT_LABELS[] = {StrId::STR_BAR_LEFT_1, StrId::STR_BAR_LEFT_2,  StrId::STR_BAR_LEFT_3,
                                 StrId::STR_CENTER,     StrId::STR_BAR_RIGHT_1, StrId::STR_BAR_RIGHT_2,
                                 StrId::STR_BAR_RIGHT_3};
constexpr StrId ITEM_LABELS[] = {
    StrId::STR_HIDE,
    StrId::STR_CLOCK,
    StrId::STR_BATTERY,
    StrId::STR_CHAPTER_PAGE_COUNT,
    StrId::STR_BOOK_PROGRESS_PERCENTAGE,
    StrId::STR_BAR_BOOK_TITLE,
    StrId::STR_BAR_CHAPTER_TITLE,
    StrId::STR_BAR_DATE,
    StrId::STR_BAR_TIME_BOOK,
    StrId::STR_BAR_TIME_SECTION,
#if defined(CROSSPOINT_EXPERIMENTAL_CHAPTER_TIME)
    StrId::STR_BAR_TIME_CHAPTER,
#endif
    StrId::STR_BAR_CHAPTER_PERCENT,
};
static_assert(std::size(ITEM_LABELS) == static_cast<size_t>(ReaderBarItem::Count));
constexpr StrId PROGRESS_LABELS[] = {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE};
constexpr StrId THICKNESS_LABELS[] = {StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM,
                                      StrId::STR_PROGRESS_BAR_THICK};
constexpr StrId DECIMAL_LABELS[] = {StrId::STR_BAR_PERCENT_WHOLE, StrId::STR_BAR_PERCENT_ONE,
                                    StrId::STR_BAR_PERCENT_TWO};
constexpr StrId BATTERY_LABELS[] = {StrId::STR_BAR_BATTERY_GLOBAL, StrId::STR_BAR_BATTERY_ICON_PERCENT,
                                    StrId::STR_BAR_BATTERY_ICON_ONLY, StrId::STR_BAR_BATTERY_PERCENT_ONLY};
}  // namespace

ReaderBar& ReaderBarsSettingsActivity::bar() {
  return view == View::Top ? SETTINGS.readerTopBar : SETTINGS.readerBottomBar;
}

const char* ReaderBarsSettingsActivity::headerTitle() const {
  switch (view) {
    case View::Root:
      return tr(STR_READER_BARS);
    case View::Top:
      return tr(STR_BAR_TOP);
    case View::Bottom:
      return tr(STR_BAR_BOTTOM);
  }
  return tr(STR_READER_BARS);
}

bool ReaderBarsSettingsActivity::handleCustomInput() {
  return popup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void ReaderBarsSettingsActivity::onBackButton() {
  if (view == View::Root) {
    finish();
    return;
  }
  view = View::Root;
  nav.reset();
  requestUpdate();
}

void ReaderBarsSettingsActivity::activateIndex(int index) {
  if (popup.isActive() || index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  if (view == View::Root) {
    if (index == 0) {
      SETTINGS.readerBarsEnabled = !SETTINGS.readerBarsEnabled;
      SETTINGS.saveToFile();
    } else {
      view = index == 1 ? View::Top : View::Bottom;
      nav.reset();
    }
  } else if (index < 7) {
    const View selectedView = view;
    auto picker = makeUniqueNoThrow<EnumSelectionActivity>(renderer, mappedInput, SLOT_LABELS[index],
                                                           std::span<const StrId>(ITEM_LABELS),
                                                           static_cast<int>(bar().slots[index]));
    if (!picker) {
      LOG_ERR("SETTINGS", "OOM: reader bar item picker");
      requestUpdate();
      return;
    }
    startActivityForResult(std::move(picker), [this, selectedView, index](const ActivityResult& result) {
      const auto* selected = std::get_if<MenuResult>(&result.data);
      if (!result.isCancelled && selected && view == selectedView && selected->action >= 0 &&
          selected->action < static_cast<int>(ReaderBarItem::Count)) {
        bar().slots[index] = static_cast<ReaderBarItem>(selected->action);
        SETTINGS.saveToFile();
      }
      requestUpdate();
    });
  } else if (index == 7) {
    popup.show(StrId::STR_PROGRESS_BAR, PROGRESS_LABELS, 3, bar().progress, [this](int selected) {
      bar().progress = selected;
      SETTINGS.saveToFile();
    });
  } else if (index == 8) {
    popup.show(StrId::STR_PROGRESS_BAR_THICKNESS, THICKNESS_LABELS, 3, bar().thickness, [this](int selected) {
      bar().thickness = selected;
      SETTINGS.saveToFile();
    });
  } else if (index == 9) {
    popup.show(StrId::STR_BAR_PERCENT_FORMAT, DECIMAL_LABELS, 3, bar().percentageDecimals, [this](int selected) {
      bar().percentageDecimals = selected;
      SETTINGS.saveToFile();
    });
  } else if (index == 10 || index == 11) {
    auto& value = index == 10 ? bar().hidden : bar().centered;
    value = !value;
    SETTINGS.saveToFile();
  } else if (index == 12) {
    popup.show(StrId::STR_BAR_BATTERY_STYLE, BATTERY_LABELS, 4, static_cast<int>(bar().batteryStyle),
               [this](int selected) {
                 bar().batteryStyle = static_cast<ReaderBarBatteryStyle>(selected);
                 SETTINGS.saveToFile();
               });
  } else {
    const char* labels[] = {
        readerBarSeparatorGlyph(ReaderBarSeparator::Bullet), readerBarSeparatorGlyph(ReaderBarSeparator::Dash),
        readerBarSeparatorGlyph(ReaderBarSeparator::Pipe), readerBarSeparatorGlyph(ReaderBarSeparator::MiddleDot)};
    popup.show(tr(STR_BAR_SEPARATOR), labels, 4, static_cast<int>(bar().separator), [this](int selected) {
      bar().separator = static_cast<ReaderBarSeparator>(selected);
      SETTINGS.saveToFile();
    });
  }
  requestUpdate();
}

void ReaderBarsSettingsActivity::render(RenderLock&& lock) {
  if (popup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}

void ReaderBarsSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  for (int i = 0; i < listCount(); ++i) {
    rows[i] = {};
    rows[i].actionValue = i;
    if (view == View::Root) {
      rows[i].label = i == 0 ? tr(STR_BAR_USE_CUSTOM) : i == 1 ? tr(STR_BAR_TOP) : tr(STR_BAR_BOTTOM);
      if (i == 0) GUI.setCheckboxRow(rows[i], SETTINGS.readerBarsEnabled);
    } else if (i < 7) {
      rows[i].label = I18N.get(SLOT_LABELS[i]);
      rows[i].value = I18N.get(ITEM_LABELS[static_cast<int>(bar().slots[i])]);
    } else if (i == 7) {
      rows[i].label = tr(STR_PROGRESS_BAR);
      rows[i].value = I18N.get(PROGRESS_LABELS[bar().progress]);
    } else if (i == 8) {
      rows[i].label = tr(STR_PROGRESS_BAR_THICKNESS);
      rows[i].value = I18N.get(THICKNESS_LABELS[bar().thickness]);
    } else if (i == 9) {
      rows[i].label = tr(STR_BAR_PERCENT_FORMAT);
      rows[i].value = I18N.get(DECIMAL_LABELS[bar().percentageDecimals]);
    } else if (i == 10 || i == 11) {
      rows[i].label = i == 10 ? tr(STR_BAR_HIDDEN) : tr(STR_BAR_CENTERED);
      GUI.setCheckboxRow(rows[i], i == 10 ? bar().hidden : bar().centered);
    } else if (i == 12) {
      rows[i].label = tr(STR_BAR_BATTERY_STYLE);
      rows[i].value = I18N.get(BATTERY_LABELS[static_cast<unsigned>(bar().batteryStyle)]);
    } else {
      rows[i].label = tr(STR_BAR_SEPARATOR);
      rows[i].value = readerBarSeparatorGlyph(bar().separator);
    }
  }
  fui::ListProps props;
  props.items = rows;
  props.count = listCount();
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
