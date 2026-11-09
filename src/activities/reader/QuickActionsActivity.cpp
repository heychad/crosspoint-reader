#include "QuickActionsActivity.h"

#include <HalFrontlight.h>
#include <HalTiltSensor.h>

#include "CrossPointSettings.h"
#include "components/UITheme.h"

using readershortcuts::Action;

QuickActionsActivity::QuickActionsActivity(GfxRenderer& renderer, MappedInputManager& input, Mode mode,
                                           uint64_t allowedActions)
    : UiListActivity("QuickActions", renderer, input), mode(mode), allowedActions(allowedActions) {
  const auto remove = [this](Action action) {
    this->allowedActions &= ~(uint64_t{1} << static_cast<unsigned>(action));
  };
  if (!input.hasTouch()) remove(Action::ToggleTouch);
  if (!halTiltSensor.isAvailable()) remove(Action::ToggleTilt);
  if (!Frontlight.present()) {
    remove(Action::ToggleFrontlight);
    remove(Action::Brighter);
    remove(Action::Dimmer);
  }
  if (!Frontlight.present() || !Frontlight.hasColorTemperature()) {
    remove(Action::Warmer);
    remove(Action::Cooler);
  }
}

void QuickActionsActivity::onEnter() {
  UiListActivity::onEnter();
  reload();
}

void QuickActionsActivity::reload() {
  visibleCount = 0;
  for (const uint8_t value : SETTINGS.readerQuickActions) {
    if (readerquickactions::isSlotAction(value) && (allowedActions & (uint64_t{1} << value))) {
      visibleActions[visibleCount++] = static_cast<Action>(value);
    }
  }
}

int QuickActionsActivity::listCount() const {
  switch (mode) {
    case Mode::Run:
      return visibleCount + 1;
    case Mode::Configure:
      return static_cast<int>(readerquickactions::SLOT_COUNT);
    case Mode::Choose:
      return optionCount;
  }
  return 0;
}

const char* QuickActionsActivity::headerTitle() const {
  switch (mode) {
    case Mode::Run:
      return tr(STR_QUICK_ACTIONS);
    case Mode::Configure:
      return tr(STR_CONFIGURE_QUICK_ACTIONS);
    case Mode::Choose:
      return I18N.get(SLOT_LABELS[selectedSlot]);
  }
  return tr(STR_QUICK_ACTIONS);
}

void QuickActionsActivity::changeMode(Mode next, int selection) {
  RenderLock lock(*this);
  mode = next;
  nav.reset(selection);
  resetUi();
  reload();
  requestUpdate();
}

void QuickActionsActivity::onBackButton() {
  if (mode == Mode::Choose) {
    changeMode(Mode::Configure, selectedSlot);
  } else if (mode == Mode::Configure && returnToRun) {
    returnToRun = false;
    changeMode(Mode::Run);
  } else
    finish();
}

void QuickActionsActivity::openSlot(int slot) {
  selectedSlot = slot;
  optionCount = 1;
  optionActions[0] = Action::Default;
  int selected = 0;
  for (unsigned value = 2; value < static_cast<unsigned>(Action::Count); ++value) {
    if (!readerquickactions::isSlotAction(value) || !(allowedActions & (uint64_t{1} << value))) continue;
    optionActions[optionCount] = static_cast<Action>(value);
    if (value == SETTINGS.readerQuickActions[slot]) selected = optionCount;
    ++optionCount;
  }
  changeMode(Mode::Choose, selected);
}

void QuickActionsActivity::activateIndex(int index) {
  switch (mode) {
    case Mode::Run:
      if (index == visibleCount) {
        returnToRun = true;
        changeMode(Mode::Configure);
      } else if (index >= 0 && index < visibleCount) {
        setResult(MenuResult{static_cast<int>(visibleActions[index])});
        finish();
      }
      return;
    case Mode::Configure:
      if (index >= 0 && index < static_cast<int>(readerquickactions::SLOT_COUNT)) openSlot(index);
      return;
    case Mode::Choose:
      if (index < 0 || index >= optionCount) return;
      {
        RenderLock lock(*this);
        SETTINGS.readerQuickActions[selectedSlot] = static_cast<uint8_t>(optionActions[index]);
      }
      SETTINGS.saveToFile();
      changeMode(Mode::Configure, selectedSlot);
      return;
  }
}

void QuickActionsActivity::provideRow(void* context, uint16_t index, freeink::ui::ListItem& item) {
  const auto* self = static_cast<QuickActionsActivity*>(context);
  item = {};
  if (index >= self->listCount()) return;
  item.actionValue = static_cast<int16_t>(index);
  switch (self->mode) {
    case Mode::Run:
      item.label = index == self->visibleCount ? tr(STR_CONFIGURE_QUICK_ACTIONS)
                                               : I18N.get(readershortcuts::label(self->visibleActions[index]));
      return;
    case Mode::Configure: {
      item.label = I18N.get(SLOT_LABELS[index]);
      const auto value = SETTINGS.readerQuickActions[index];
      item.value = readerquickactions::isSlotAction(value)
                       ? I18N.get(readershortcuts::label(static_cast<Action>(value)))
                       : tr(STR_NONE_OPT);
      return;
    }
    case Mode::Choose:
      item.label = index == 0 ? tr(STR_NONE_OPT) : I18N.get(readershortcuts::label(self->optionActions[index]));
      return;
  }
}

void QuickActionsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(freeink::ui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight),
                                                        0, static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  freeink::ui::ListProps props;
  props.rowProvider = &QuickActionsActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = freeink::ui::InputTouch;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
