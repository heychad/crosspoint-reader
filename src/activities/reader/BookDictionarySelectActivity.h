#pragma once

#include "activities/UiListActivity.h"
#include "util/DictionaryRegistry.h"

// Owns discovered folder names; no dictionary data stays open while choosing.
class BookDictionarySelectActivity final : public UiListActivity {
 public:
  BookDictionarySelectActivity(GfxRenderer& renderer, MappedInputManager& input, bool useGlobal,
                               std::string currentName)
      : UiListActivity("BookDictionary", renderer, input), useGlobal(useGlobal), currentName(std::move(currentName)) {}
  void onEnter() override;

 private:
  bool useGlobal;
  std::string currentName;
  std::vector<DictionaryEntry> dictionaries;
  int listCount() const override { return static_cast<int>(dictionaries.size()) + 2; }
  const char* headerTitle() const override { return tr(STR_DICTIONARY); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  static void provideRow(void* context, uint16_t index, freeink::ui::ListItem& row);
};
