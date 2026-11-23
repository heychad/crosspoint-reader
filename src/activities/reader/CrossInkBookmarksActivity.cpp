#include "CrossInkBookmarksActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#include "CrossInkBookmarkEdit.h"
#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;
namespace {
constexpr int CONFIRM_HOLD_MS = 700;
}

CrossInkBookmarksActivity::CrossInkBookmarksActivity(GfxRenderer& renderer, MappedInputManager& input,
                                                     std::string bookPath)
    : UiListActivity("CrossInkBookmarks", renderer, input, /*wantsTouchLongPress=*/true),
      bookPath(std::move(bookPath)) {}

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
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  syncListViewport(screen, props);
#if FREEINK_CAP_TOUCH
  configureSwipeDelete(screen, props);
#endif
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

bool CrossInkBookmarksActivity::handleButtons() {
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, CONFIRM_HOLD_MS)) {
    onRowLongPress(nav.selected);
    return true;
  }
  return UiListActivity::handleButtons();
}

#if FREEINK_CAP_TOUCH
bool CrossInkBookmarksActivity::canSwipeDelete(const int index) const {
  return index >= 0 && index < listCount() && bookmarks.canRemove(static_cast<uint16_t>(index));
}

void CrossInkBookmarksActivity::swipeDelete(const int index) { onRowLongPress(index); }
#endif

void CrossInkBookmarksActivity::onRowLongPress(const int index) {
  if (index < 0 || index >= listCount() || !bookmarks.canRemove(static_cast<uint16_t>(index))) return;
  crossinkbookmarks::RemovalTarget target;
  CrossInkBookmarks::Entry selected;
  bool captured;
  {
    RenderLock lock(*this);
    captured = bookmarks.selectForRemoval(static_cast<uint16_t>(index), selected, target);
  }
  app.clearTapFlash();
  if (!captured) {
    showNotice(tr(STR_SAVED_ITEMS_ERROR));
    return;
  }
  const char* text = selected.snippet[0] ? selected.snippet : selected.chapter;
  const std::string headline(text, utf8SafeTruncateBuffer(text, strlen(text)));
  auto prompt = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_CONFIRM_DELETE_BOOKMARK),
                                                        headline, tr(STR_CANCEL), tr(STR_DELETE));
  if (!prompt) {
    LOG_ERR("BKM", "Cannot allocate CrossInk bookmark confirmation");
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(prompt), [this, index, target = std::move(target)](const ActivityResult& result) {
    if (!result.isCancelled) deleteBookmark(index, target);
  });
}

void CrossInkBookmarksActivity::deleteBookmark(const int index, const crossinkbookmarks::RemovalTarget& target) {
  using legacybookmarkedit::Result;
  Result removal = Result::Unavailable;
  {
    RenderLock lock(*this);
    if (bookmarks.close()) removal = legacybookmarkedit::removeSelected(bookPath, target);
    bookmarks.open(bookPath);
  }
  if (listCount() > 0) {
    moveSelectionTo(std::min(index, listCount() - 1));
  } else {
    nav.reset();
    requestUpdate();
  }
  if (removal == Result::Published) {
    showNotice(tr(STR_BOOKMARK_DELETE_RECOVERY));
  } else if (removal != Result::Archived) {
    LOG_ERR("BKM", "CrossInk bookmark delete incomplete (%d)", static_cast<int>(removal));
    showNotice(tr(STR_BOOKMARK_SAVE_INCOMPLETE));
  }
}

void CrossInkBookmarksActivity::showNotice(const char* message) {
  auto notice = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_CROSSINK_BOOKMARKS), message, "",
                                                        tr(STR_OK_BUTTON), true);
  if (notice) {
    startActivityForResult(std::move(notice), [](const ActivityResult&) {});
  } else {
    LOG_ERR("BKM", "Cannot allocate CrossInk bookmark notice");
    requestUpdate();
  }
}
