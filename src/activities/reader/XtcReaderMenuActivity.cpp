#include "XtcReaderMenuActivity.h"

#include "CrossPointSettings.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

size_t XtcReaderMenuActivity::buildItems(std::array<MenuItem, MAX_ITEMS>& items, bool hasChapters, bool hasTouch) {
  size_t count = 0;
  if (hasChapters) items[count++] = {MenuAction::Chapters, StrId::STR_SELECT_CHAPTER};
  items[count++] = {MenuAction::Percent, StrId::STR_GO_TO_PERCENT};
  items[count++] = {MenuAction::Statistics, StrId::STR_READING_STATISTICS};
  items[count++] = {MenuAction::QuickActions, StrId::STR_QUICK_ACTIONS};
  items[count++] = {MenuAction::Controls, StrId::STR_CAT_CONTROLS};
  items[count++] = {MenuAction::Library, StrId::STR_LIBRARY};
  items[count++] = {MenuAction::Home, StrId::STR_GO_HOME_BUTTON};
  if (hasTouch) items[count++] = {MenuAction::Touch, StrId::STR_TOUCH_READER_CONTROLS};
  return count;
}

XtcReaderMenuActivity::XtcReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& input, std::string title,
                                             bool hasChapters, bool hasTouch)
    : UiListActivity("XtcReaderMenu", renderer, input),
      title(std::move(title)),
      count(buildItems(items, hasChapters, hasTouch)) {}

void XtcReaderMenuActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

bool XtcReaderMenuActivity::handleHomeGesture() {
  cancel();
  return true;
}

bool XtcReaderMenuActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancel();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }
  return false;
}

void XtcReaderMenuActivity::activateIndex(int index) {
  if (index < 0 || index >= static_cast<int>(count)) return;
  app.clearTapFlash();
  if (items[index].action == MenuAction::Touch) {
    {
      RenderLock lock;
      SETTINGS.touchReaderControls = SETTINGS.touchReaderControls ? 0 : 1;
    }
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }
  setResult(MenuResult{static_cast<int>(items[index].action)});
  finish();
}

void XtcReaderMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  for (size_t i = 0; i < count; ++i) {
    rows[i] = {};
    rows[i].label = I18N.get(items[i].label);
    rows[i].actionValue = static_cast<int16_t>(i);
    if (items[i].action == MenuAction::Touch) GUI.setCheckboxRow(rows[i], SETTINGS.touchReaderControls != 0);
  }
  fui::ListProps props;
  props.items = rows.data();
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
