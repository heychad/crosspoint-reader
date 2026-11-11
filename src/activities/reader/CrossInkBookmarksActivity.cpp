#include "CrossInkBookmarksActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#include "components/UITheme.h"

namespace fui = freeink::ui;

CrossInkBookmarksActivity::CrossInkBookmarksActivity(GfxRenderer& renderer, MappedInputManager& input,
                                                     std::string bookPath)
    : UiListActivity("CrossInkBookmarks", renderer, input), bookPath(std::move(bookPath)) {}

void CrossInkBookmarksActivity::onEnter() {
  RenderLock lock(*this);
  UiListActivity::onEnter();
  if (!bookmarks.open(bookPath)) LOG_ERR("BKM", "Cannot load CrossInk bookmarks");
}

void CrossInkBookmarksActivity::onExit() {
  bookmarks.close();
  UiListActivity::onExit();
}

const char* CrossInkBookmarksActivity::headerTitle() const { return tr(STR_CROSSINK_BOOKMARKS); }

void CrossInkBookmarksActivity::provideRow(void* context, const uint16_t index, fui::ListItem& row) {
  auto& self = *static_cast<CrossInkBookmarksActivity*>(context);
  auto& entry = self.entryScratch;
  const bool valid = self.bookmarks.read(index, entry);
  const char* text = entry.snippet[0] ? entry.snippet : entry.chapter;
  snprintf(self.label, sizeof(self.label), "%s",
           valid ? (*text ? text : tr(STR_BOOKMARK_OPTION)) : tr(STR_SAVED_ITEMS_ERROR));
  self.label[utf8SafeTruncateBuffer(self.label, strlen(self.label))] = '\0';
  self.subtitle[0] = '\0';
  if (valid) {
    snprintf(self.subtitle, sizeof(self.subtitle),
             I18N.get(entry.recovered ? StrId::STR_SAVED_BOOKMARK_BACKUP_LOCATION : StrId::STR_SAVED_BOOKMARK_LOCATION),
             static_cast<unsigned>(entry.spine + 1), static_cast<int>(entry.progress * 100));
  }
  row.label = self.label;
  row.subtitle = valid ? self.subtitle : "";
  row.actionValue = index;
}

void CrossInkBookmarksActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height), static_cast<int16_t>(safe.x)});
  if (!listCount()) {
    screen.centeredText((bookmarks.unreadable() ? tr(STR_SAVED_ITEMS_ERROR) : tr(STR_NO_BOOKMARKS)),
                        screen.theme().bodyText);
    return;
  }
  // Refresh theme defaults without constructing the large descriptor on the stack.
  std::destroy_at(&listProps);
  std::construct_at(&listProps);
  auto& props = listProps;
  props.count = listCount();
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  props.rowProvider = &CrossInkBookmarksActivity::provideRow;
  props.rowProviderCtx = this;
  screen.list(props);
}

void CrossInkBookmarksActivity::activateIndex(const int index) {
  ProgressChangeResult result;
  {
    RenderLock lock(*this);
    if (index < 0 || index >= listCount() || !bookmarks.read(index, entryScratch)) return;
    result.spineIndex = entryScratch.spine;
    result.hasChapterProgress = true;
    result.chapterProgress = entryScratch.progress;
    result.paragraphIndex = entryScratch.paragraph;
  }
  app.clearTapFlash();
  setResult(std::move(result));
  finish();
}
