#pragma once

#include <span>

#include "activities/UiListActivity.h"

// Labels are borrowed from the retained parent settings list or static storage.
// The parent may rebuild that list only after this activity has been destroyed.
class EnumSelectionActivity final : public UiListActivity {
 public:
  EnumSelectionActivity(GfxRenderer& renderer, MappedInputManager& input, StrId title, std::span<const StrId> labels,
                        int selected);
  void onEnter() override;

 private:
  StrId title;
  std::span<const StrId> labels;
  int initialSelection;
  int listCount() const override { return static_cast<int>(labels.size()); }
  const char* headerTitle() const override { return I18N.get(title); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  static void provideRow(void* context, uint16_t index, freeink::ui::ListItem& item);
};
