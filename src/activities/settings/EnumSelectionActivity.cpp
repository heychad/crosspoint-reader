#include "EnumSelectionActivity.h"

#include <algorithm>

#include "components/UITheme.h"

EnumSelectionActivity::EnumSelectionActivity(GfxRenderer& renderer, MappedInputManager& input, StrId title,
                                             std::span<const StrId> labels, int selected)
    : UiListActivity("EnumSelection", renderer, input), title(title), labels(labels), initialSelection(selected) {}

void EnumSelectionActivity::onEnter() {
  UiListActivity::onEnter();
  nav.reset(std::clamp(initialSelection, 0, std::max(0, listCount() - 1)));
}

void EnumSelectionActivity::activateIndex(int index) {
  if (index < 0 || index >= listCount()) return;
  setResult(MenuResult{index});
  finish();
}

void EnumSelectionActivity::provideRow(void* context, uint16_t index, freeink::ui::ListItem& item) {
  const auto* self = static_cast<EnumSelectionActivity*>(context);
  item = {};
  if (index >= self->labels.size()) return;
  item.label = I18N.get(self->labels[index]);
  item.actionValue = static_cast<int16_t>(index);
}

void EnumSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(freeink::ui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight),
                                                        0, static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  freeink::ui::ListProps props;
  props.rowProvider = &EnumSelectionActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = freeink::ui::InputTouch;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
