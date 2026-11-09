#include "DeviceDiagnosticsActivity.h"

#include <HalMemory.h>
#include <I18n.h>

#include <cstdio>
#include <utility>

#include "components/UITheme.h"

namespace fui = freeink::ui;

DeviceDiagnosticsActivity::DeviceDiagnosticsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("DeviceDiagnostics", renderer, mappedInput) {}

const char* DeviceDiagnosticsActivity::headerTitle() const { return tr(STR_DEVICE_DIAGNOSTICS); }

void DeviceDiagnosticsActivity::onEnter() {
  static constexpr StrId labels[] = {StrId::STR_DIAG_INTERNAL_FREE, StrId::STR_DIAG_INTERNAL_LARGEST,
                                     StrId::STR_DIAG_INTERNAL_MIN,  StrId::STR_DIAG_PSRAM_FREE,
                                     StrId::STR_DIAG_PSRAM_LARGEST, StrId::STR_DIAG_PSRAM_MIN,
                                     StrId::STR_DIAG_REFRESH};
  RenderLock lock(*this);
  for (int i = 0; i < ITEM_COUNT; ++i) {
    rows[i].label = I18N.get(labels[i]);
    rows[i].value = i < VALUE_COUNT ? values[i] : nullptr;
    rows[i].actionValue = static_cast<int16_t>(i);
  }
  sample();
  UiListActivity::onEnter();
  initialized = true;
}

void DeviceDiagnosticsActivity::render(RenderLock&& lock) {
  if (!initialized) return;
  UiListActivity::render(std::move(lock));
}

void DeviceDiagnosticsActivity::sample() {
  const auto internal = HalMemory::getInternalHeap();
  const auto psram = HalMemory::getPsramHeap();
  const size_t samples[] = {internal.freeBytes, internal.largestBlockBytes, internal.minFreeBytes,
                            psram.freeBytes,    psram.largestBlockBytes,    psram.minFreeBytes};
  for (int i = 0; i < VALUE_COUNT; ++i)
    snprintf(values[i], sizeof(values[i]), "%lu B", static_cast<unsigned long>(samples[i]));
}

void DeviceDiagnosticsActivity::activateIndex(int index) {
  if (index != VALUE_COUNT) return;
  RenderLock lock(*this);
  app.clearTapFlash();
  sample();
  requestUpdate();
}

void DeviceDiagnosticsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = rows;
  props.count = ITEM_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
