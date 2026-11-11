#include "BookDictionarySelectActivity.h"

#include <algorithm>

#include "components/UITheme.h"

void BookDictionarySelectActivity::onEnter() {
  RenderLock lock(*this);
  UiListActivity::onEnter();
  DictionaryRegistry::discover(dictionaries);
  // The persisted name has the same 32-byte capacity as the global setting.
  dictionaries.erase(std::remove_if(dictionaries.begin(), dictionaries.end(),
                                    [](const auto& entry) { return entry.name.size() >= 32; }),
                     dictionaries.end());
  int selected = useGlobal ? 0 : 1;
  if (!useGlobal && !currentName.empty()) {
    for (size_t index = 0; index < dictionaries.size(); ++index)
      if (dictionaries[index].name == currentName) selected = static_cast<int>(index) + 2;
  }
  nav.reset(selected);
}

void BookDictionarySelectActivity::provideRow(void* context, uint16_t index, freeink::ui::ListItem& row) {
  const auto& self = *static_cast<BookDictionarySelectActivity*>(context);
  row = {};
  if (index >= self.listCount()) return;
  row.label = index == 0   ? tr(STR_BOOK_DICTIONARY_GLOBAL)
              : index == 1 ? tr(STR_NONE_OPT)
                           : self.dictionaries[index - 2].name.c_str();
  row.actionValue = index;
}

void BookDictionarySelectActivity::activateIndex(int index) {
  if (index < 0 || index >= listCount()) return;
  setResult(BookDictionaryResult{index == 0, index > 1 ? dictionaries[index - 2].name : std::string{}});
  finish();
}

void BookDictionarySelectActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(freeink::ui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight),
                                                        0, static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  freeink::ui::ListProps props;
  props.rowProvider = &BookDictionarySelectActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = freeink::ui::InputTouch;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
