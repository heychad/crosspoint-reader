#pragma once

#include "activities/UiListActivity.h"

class DeviceDiagnosticsActivity final : public UiListActivity {
 public:
  DeviceDiagnosticsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void render(RenderLock&& lock) override;

 private:
  static constexpr int VALUE_COUNT = 6;
  static constexpr int ITEM_COUNT = VALUE_COUNT + 1;
  char values[VALUE_COUNT][24]{};
  freeink::ui::ListItem rows[ITEM_COUNT]{};
  bool initialized = false;
  void sample();
  int listCount() const override { return ITEM_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
};
