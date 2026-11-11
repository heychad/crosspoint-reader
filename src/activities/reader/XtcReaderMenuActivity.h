#pragma once

#include <array>
#include <string>

#include "activities/UiListActivity.h"

class XtcReaderMenuActivity final : public UiListActivity {
 public:
  enum class MenuAction {
    Chapters,
    Percent,
    Statistics,
    QuickActions,
    Controls,
    Library,
    Home,
    Touch,
    Tracking,
    Completion
  };
  struct MenuItem {
    MenuAction action;
    StrId label;
  };
  static constexpr size_t MAX_ITEMS = 10;
  static size_t buildItems(std::array<MenuItem, MAX_ITEMS>& items, bool hasChapters, bool hasTouch,
                           bool completionKnown = false, bool completed = false);
  XtcReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& input, std::string title, bool hasChapters,
                        bool hasTouch, bool completionKnown = false, bool completed = false);
  bool handleHomeGesture() override;

 private:
  std::string title;
  std::array<MenuItem, MAX_ITEMS> items{};
  std::array<freeink::ui::ListItem, MAX_ITEMS> rows{};
  size_t count = 0;
  int listCount() const override { return static_cast<int>(count); }
  const char* headerTitle() const override { return title.c_str(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleButtons() override;
  void cancel();
};
