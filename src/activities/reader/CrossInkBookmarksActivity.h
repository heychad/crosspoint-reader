#pragma once

#include "activities/UiListActivity.h"
#include "util/CrossInkBookmarks.h"

class CrossInkBookmarksActivity final : public UiListActivity {
 public:
  CrossInkBookmarksActivity(GfxRenderer& renderer, MappedInputManager& input, std::string bookPath);
  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override { return bookmarks.count(); }
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  freeink::ui::ListProps listProps;
  void activateIndex(int index) override;
  static void provideRow(void* context, uint16_t index, freeink::ui::ListItem& row);
  std::string bookPath;
  CrossInkBookmarks bookmarks;
  CrossInkBookmarks::Entry entryScratch;
  char label[64]{};
  char subtitle[80]{};
};
