#pragma once

#include "activities/UiListActivity.h"

class DeviceDiagnosticsActivity final : public UiListActivity {
 public:
  DeviceDiagnosticsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void render(RenderLock&& lock) override;

 private:
  static constexpr int MEMORY_COUNT = 6;
  static constexpr int REFRESH_ROW = MEMORY_COUNT;
#if FREEINK_DEVICE_X4PRO
  static constexpr int RECORDING_ROW = REFRESH_ROW + 1;
  static constexpr int TIMING_FIRST_ROW = RECORDING_ROW + 1;
  static constexpr int TIMING_COUNT = 3;
  static constexpr int ITEM_COUNT = TIMING_FIRST_ROW + TIMING_COUNT;
  char timingValues[TIMING_COUNT][32]{};
#else
  static constexpr int ITEM_COUNT = REFRESH_ROW + 1;
#endif
  char values[MEMORY_COUNT][24]{};
  freeink::ui::ListItem rows[ITEM_COUNT]{};
  bool initialized = false;
  void sample();
  int listCount() const override { return ITEM_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
};
