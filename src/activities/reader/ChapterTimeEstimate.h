#pragma once

#include <cstdint>
#include <limits>

namespace chaptertime {
struct Extent {
  uint64_t currentBytes = 0;
  uint64_t laterBytes = 0;
};

// The metadata cache maps each spine item to its first TOC entry, carrying the
// last entry forward over unlabeled items. Refuse fragment boundaries because
// their position within a spine item cannot be inferred from byte sizes.
template <class Book>
bool extent(Book& book, int spine, Extent& output) {
  const int spines = book.getSpineItemsCount();
  const int tocCount = book.getTocItemsCount();
  if (spine < 0 || spine >= spines || tocCount <= 0) return false;
  const int toc = book.getTocIndexForSpineIndex(spine);
  if (toc < 0 || toc >= tocCount) return false;
  const auto current = book.getTocItem(toc);
  if (current.spineIndex < 0 || current.spineIndex > spine || !current.anchor.empty()) return false;
  int last = spines - 1;
  if (toc + 1 < tocCount) {
    const auto next = book.getTocItem(toc + 1);
    if (next.spineIndex <= spine || next.spineIndex >= spines || !next.anchor.empty()) return false;
    last = next.spineIndex - 1;
  }
  const uint64_t previous = spine ? book.getCumulativeSpineItemSize(spine - 1) : 0;
  const uint64_t currentEnd = book.getCumulativeSpineItemSize(spine);
  const uint64_t groupEnd = book.getCumulativeSpineItemSize(last);
  if (currentEnd <= previous || groupEnd < currentEnd) return false;
  output = {currentEnd - previous, groupEnd - currentEnd};
  return true;
}

// Current section pages are exact. Later pages are projected from the ratio
// of uncompressed spine bytes to pages in the current section.
inline bool remainingPages(const Extent& extent, int currentPage, int sectionPages, int& output) {
  if (!extent.currentBytes || currentPage < 1 || sectionPages < currentPage ||
      extent.laterBytes >
          (std::numeric_limits<uint64_t>::max() - (extent.currentBytes - 1)) / static_cast<uint64_t>(sectionPages))
    return false;
  const uint64_t later =
      (extent.laterBytes * static_cast<uint64_t>(sectionPages) + extent.currentBytes - 1) / extent.currentBytes;
  const uint64_t remaining = static_cast<uint64_t>(sectionPages - currentPage) + later;
  if (remaining > static_cast<uint64_t>(std::numeric_limits<int>::max() - currentPage)) return false;
  output = static_cast<int>(remaining);
  return true;
}
}  // namespace chaptertime
