#include "LibraryCoverGrid.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>

namespace fui = freeink::ui;

LibraryCoverGrid::LibraryCoverGrid(GfxRenderer& renderer) : coverCache(renderer) {
  static_assert(MAX_BOOKS <= HomeCoverCache::MAX_COVERS);
  coverCache.begin();
  props.columns = 3;
  props.scrollIndicator = false;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.selectionIndicator = fui::CoverGridSelectionIndicator::CoverFrame;
  props.selectedCoverFrameGap = 3;
  props.selectedCoverFrameWidth = 3;
  props.cellInset = {4, 6, 0, 6};
  props.labelFollowsCover = true;
  props.itemProviderUserData = this;
  props.itemProvider = [](uint16_t slot, void* context) {
    auto& self = *static_cast<LibraryCoverGrid*>(context);
    const auto& book = self.books[slot];
    return fui::coverGridItem(book.title.data(), self.firstEntry + slot, book.present);
  };
  props.coverPainterUserData = this;
  props.coverPainter = [](fui::DrawTarget&, fui::Rect rect, const fui::CoverGridItem&, uint16_t slot, void* context) {
    auto& self = *static_cast<LibraryCoverGrid*>(context);
    return self.coverCache.paint(rect, slot, self.books[slot].coverPath.data());
  };
}

bool LibraryCoverGrid::layout(UiAppHost::UiScreen& screen) {
  bounds = screen.body().inset({4, 12, 0, 12});
  props.titleText = screen.theme().smallText;
  props.titleText.maxLines = 2;
  props.gap = props.rowGap = screen.theme().spaceSm;
  if (!fui::fitCoverGrid(bounds, MAX_BOOKS, screen.target().lineHeight(props.titleText.font), 400, props)) {
    visibleBooks = 0;
    return false;
  }
  const int page =
      std::min<int>(MAX_BOOKS, fui::coverGridVisibleCells(bounds, props.columns, props.rowHeight, props.rowGap));
  if (page == 0) {
    visibleBooks = 0;
    return false;
  }
  if (coverHeight != props.coverSize.height) {
    coverHeight = props.coverSize.height;
    invalidate();
  }
  coverCache.prepare();
  visibleBooks = page;
  return true;
}

void LibraryCoverGrid::setBook(const int slot, const uint64_t identity, const std::string& title) {
  auto& book = books[slot];
  if (!book.present || book.identity != identity) {
    book.identity = identity;
    book.present = true;
    book.coverPath[0] = '\0';
    book.attempted = false;
    coverCache.invalidate(slot);
  }
  const size_t length =
      title.size() > 512 ? static_cast<size_t>(utf8SafeTruncateBuffer(title.data(), 512)) : title.size();
  memcpy(book.title.data(), title.data(), length);
  book.title[length] = '\0';
}

void LibraryCoverGrid::clearBook(const int slot) {
  auto& book = books[slot];
  book.present = false;
  book.attempted = true;
  book.title[0] = '\0';
  book.coverPath[0] = '\0';
  coverCache.invalidate(slot);
}

void LibraryCoverGrid::draw(UiAppHost::UiScreen& screen, const int first, const int count, const int selected,
                            const fui::ActionId action) {
  firstEntry = first;
  props.count = static_cast<uint16_t>(count);
  props.selectedIndex = selected < 0 ? -1 : static_cast<int16_t>(selected - first);
  props.action = action;
  fui::coverGrid(screen.frame(), bounds, props);
}

void LibraryCoverGrid::invalidate() {
  coverCache.invalidate();
  for (auto& book : books) {
    book.attempted = false;
    book.coverPath[0] = '\0';
  }
}

int LibraryCoverGrid::nextCover() const {
  for (int slot = 0; slot < visibleBooks; ++slot)
    if (books[slot].present && !books[slot].attempted) return slot;
  return -1;
}

bool LibraryCoverGrid::matches(const int slot, const uint64_t identity) const {
  return books[slot].present && books[slot].identity == identity;
}

bool LibraryCoverGrid::loadCover(const int slot, const std::string& path) {
  auto& book = books[slot];
  book.attempted = true;
  std::string thumb;
  if (FsHelpers::hasReflowableBookExtension(path)) {
    auto epub = makeUniqueNoThrow<Epub>(path, "/.crosspoint");
    if (!epub) {
      LOG_ERR("LIB", "OOM: EPUB cover");
      return false;
    }
    if (!epub->generateThumbBmpFromSource(coverHeight)) return false;
    thumb = epub->getThumbBmpPath(coverHeight);
  } else if (FsHelpers::hasXtcExtension(path)) {
    auto xtc = makeUniqueNoThrow<Xtc>(path, "/.crosspoint");
    if (!xtc) {
      LOG_ERR("LIB", "OOM: XTC cover");
      return false;
    }
    thumb = xtc->getThumbBmpPath(coverHeight);
    if (!Storage.exists(thumb.c_str()) && (!xtc->load() || !xtc->generateThumbBmp(coverHeight))) return false;
  } else {
    return false;
  }
  if (thumb.size() >= book.coverPath.size()) {
    LOG_ERR("LIB", "thumbnail path exceeds cover buffer");
    return false;
  }
  memcpy(book.coverPath.data(), thumb.c_str(), thumb.size() + 1);
  coverCache.invalidate(slot);
  return true;
}
