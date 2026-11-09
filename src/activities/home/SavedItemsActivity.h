#pragma once

#include <LibraryIndexFile.h>

#include <array>
#include <memory>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "util/SavedItems.h"

class SavedItemsActivity final : public UiListActivity {
 public:
  SavedItemsActivity(GfxRenderer& renderer, MappedInputManager& input);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  enum class State { Scanning, Ready, Error };
  void scanBatch();
  static void provideRow(void* context, uint16_t index, freeink::ui::ListItem& row);
  int listCount() const override;
  const char* headerTitle() const override;
  void buildScreen(UiScreen& screen) override;
  freeink::ui::ListProps listProps;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void onBackButton() override;

  library::LibraryIndexFile index;
  library::ClixRecord record{};
  library::LibraryIndexFile::PathLookup scanPaths;
  std::unique_ptr<uint16_t[]> ordinals;
  uint16_t count = 0;
  uint16_t scanned = 0;
  State state = State::Scanning;
  bool incomplete = false;
  std::string scratch;
  std::string selectedPath;
  char title[513]{};
  char author[129]{};
  std::array<SavedItemKind, 3> choices{};
  OptionPopup popup;
};
