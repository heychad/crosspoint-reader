#include "SavedItemsActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

SavedItemsActivity::SavedItemsActivity(GfxRenderer& renderer, MappedInputManager& input)
    : UiListActivity("SavedItems", renderer, input) {}

void SavedItemsActivity::onEnter() {
  RenderLock lock(*this);
  UiListActivity::onEnter();
  scratch.reserve(512);
  selectedPath.reserve(512);
  if (library::isLibraryIndexDirty() || !index.open(library::libraryIndexPath()) ||
      index.header().metadataEnabled != (SETTINGS.libraryUseMetadata != 0)) {
    index.close();
    GUI.drawPopup(renderer, tr(STR_LIBRARY_REBUILDING));
    library::BuildStats stats;
    if (!library::buildLibraryIndex("/", stats, SETTINGS.libraryUseMetadata != 0) ||
        !index.open(library::libraryIndexPath())) {
      state = State::Error;
      return;
    }
  }
  incomplete = (index.header().flags & library::CLIX_FLAG_SCAN_COMPLETE) == 0;
  if (!index.bookCount()) {
    state = State::Ready;
    return;
  }
  scanPaths = index.preparePathLookup();
  if (!scanPaths) {
    LOG_ERR("SAVED", "Cannot prepare library paths");
    state = State::Error;
    return;
  }
  ordinals = makeUniqueNoThrow<uint16_t[]>(index.bookCount());
  if (!ordinals) {
    LOG_ERR("SAVED", "OOM: saved-item rows (%zu bytes)", sizeof(uint16_t) * index.bookCount());
    state = State::Error;
  }
}

void SavedItemsActivity::onExit() {
  scanPaths = {};
  index.close();
  ordinals.reset();
  UiListActivity::onExit();
}

void SavedItemsActivity::scanBatch() {
  RenderLock lock(*this);
  const uint16_t end = std::min<int>(index.bookCount(), scanned + 4);
  for (; scanned < end; ++scanned) {
    const uint16_t ordinal = index.ordinalForRow(library::SortOrder::TitleAsc, scanned);
    if (!index.readRecord(ordinal, record) || !index.readPath(record, scratch, scanPaths)) {
      LOG_ERR("SAVED", "Failed reading library row %u", scanned);
      state = State::Error;
      requestUpdate();
      return;
    }
    if (!FsHelpers::hasReflowableBookExtension(scratch)) continue;
    if (!Storage.exists(scratch.c_str())) {
      incomplete = true;
      continue;
    }
    uint8_t kinds = 0;
    const bool readable = saveditems::inspect(scratch, kinds);
    incomplete |= !readable;
    if (kinds || !readable) ordinals[count++] = ordinal;
  }
  if (scanned == index.bookCount()) {
    scanPaths = {};
    state = State::Ready;
  }
  if (state == State::Ready || scanned % 32 == 0) requestUpdate();
}

void SavedItemsActivity::loop() {
  if (state == State::Scanning) scanBatch();
  UiListActivity::loop();
}

int SavedItemsActivity::listCount() const { return state == State::Ready ? count : 0; }
const char* SavedItemsActivity::headerTitle() const { return tr(STR_SAVED_ITEMS); }
void SavedItemsActivity::onBackButton() { activityManager.goHome(HomeMenuItem::SAVED_ITEMS); }

bool SavedItemsActivity::handleCustomInput() {
  return popup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void SavedItemsActivity::provideRow(void* context, const uint16_t rowIndex, fui::ListItem& row) {
  auto& self = *static_cast<SavedItemsActivity*>(context);
  if (rowIndex >= self.count) return;
  const bool readable =
      self.index.readRecord(self.ordinals[rowIndex], self.record) &&
      (self.index.readTitle(self.record, self.scratch) || self.index.readName(self.record, self.scratch));
  snprintf(self.title, sizeof(self.title), "%s", readable ? self.scratch.c_str() : tr(STR_SAVED_ITEMS_ERROR));
  self.title[utf8SafeTruncateBuffer(self.title, strlen(self.title))] = '\0';
  const bool hasAuthor = readable && self.index.readAuthor(self.record, self.scratch);
  snprintf(self.author, sizeof(self.author), "%s", hasAuthor ? self.scratch.c_str() : "");
  self.author[utf8SafeTruncateBuffer(self.author, strlen(self.author))] = '\0';
  row.label = self.title;
  row.subtitle = self.author;
  row.actionValue = rowIndex;
}

void SavedItemsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height), static_cast<int16_t>(safe.x)});
  if (state == State::Scanning) {
    screen.centeredText(tr(STR_LOADING), screen.theme().bodyText);
    return;
  }
  if (state == State::Error) {
    screen.centeredText(tr(STR_SAVED_ITEMS_ERROR), screen.theme().bodyText);
    return;
  }
  if (incomplete) {
    const auto band = screen.takeTop(renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing);
    GUI.drawHelpText(renderer, Rect{band.x, band.y, band.width, band.height}, tr(STR_SAVED_ITEMS_INCOMPLETE));
  }
  if (!count) {
    screen.centeredText(tr(STR_NO_SAVED_ITEMS), screen.theme().bodyText);
    return;
  }
  // Refresh theme defaults without constructing the large descriptor on the stack.
  std::destroy_at(&listProps);
  std::construct_at(&listProps);
  auto& props = listProps;
  props.count = count;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  props.rowProvider = &SavedItemsActivity::provideRow;
  props.rowProviderCtx = this;
  screen.list(props);
}

void SavedItemsActivity::activateIndex(const int row) {
  if (popup.isActive() || row < 0 || row >= listCount()) return;
  app.clearTapFlash();
  uint8_t kinds = 0;
  {
    RenderLock lock(*this);
    if (!index.readRecord(ordinals[row], record) || !index.readPath(record, selectedPath)) return;
    if (!saveditems::inspect(selectedPath, kinds)) incomplete = true;
  }
  const StrId labels[] = {StrId::STR_BOOKMARKS, StrId::STR_CROSSINK_BOOKMARKS, StrId::STR_CLIPPINGS};
  const SavedItemKind targets[] = {SavedItemKind::Bookmarks, SavedItemKind::CrossInkBookmarks,
                                   SavedItemKind::Clippings};
  const uint8_t masks[] = {saveditems::BOOKMARKS, saveditems::CROSSINK_BOOKMARKS, saveditems::HAS_CLIPPINGS};
  StrId options[3];
  int optionsCount = 0;
  for (int i = 0; i < 3; ++i) {
    if (!(kinds & masks[i])) continue;
    choices[optionsCount] = targets[i];
    options[optionsCount++] = labels[i];
  }
  if (!optionsCount) {
    const char* dismiss[] = {tr(STR_OK_BUTTON)};
    popup.showMessage(tr(STR_SAVED_ITEMS), tr(STR_SAVED_ITEMS_ERROR), dismiss, 1, 0, [](int) {});
  } else if (optionsCount == 1) {
    activityManager.goToSavedBook(selectedPath, choices[0]);
    return;
  } else {
    popup.show(StrId::STR_SAVED_ITEMS, options, optionsCount, 0,
               [this](int selected) { activityManager.goToSavedBook(selectedPath, choices[selected]); });
  }
  requestUpdate();
}

void SavedItemsActivity::render(RenderLock&& lock) {
  if (popup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}
