#include <cassert>
#include <string>
#include <vector>

#include "ChapterTimeEstimate.h"
struct Toc {
  int spineIndex;
  std::string anchor;
};
struct Book {
  std::vector<int> map;
  std::vector<uint64_t> bytes;
  std::vector<Toc> toc;
  int getSpineItemsCount() { return static_cast<int>(map.size()); }
  int getTocItemsCount() { return static_cast<int>(toc.size()); }
  int getTocIndexForSpineIndex(int i) { return map.at(i); }
  Toc getTocItem(int i) { return toc.at(i); }
  uint64_t getCumulativeSpineItemSize(int i) { return bytes.at(i); }
};
int main() {
  Book b{{0, 0, 0, 1}, {100, 300, 500, 700}, {{0, ""}, {3, ""}}};
  chaptertime::Extent e{};
  int pages = -1;
  assert(chaptertime::extent(b, 1, e) && e.currentBytes == 200 && e.laterBytes == 200);
  assert(chaptertime::remainingPages(e, 3, 10, pages) && pages == 17);
  assert(chaptertime::extent(b, 2, e) && e.currentBytes == 200 && e.laterBytes == 0);
  assert(chaptertime::remainingPages(e, 10, 10, pages) && pages == 0);
  assert(chaptertime::extent(b, 3, e) && e.laterBytes == 0);
  b.toc[1].anchor = "fragment";
  assert(!chaptertime::extent(b, 1, e));
  b.toc[1] = {1, "part"};
  assert(!chaptertime::extent(b, 1, e));
  b.toc[1] = {3, ""};
  b.toc[0].anchor = "start";
  assert(!chaptertime::extent(b, 1, e));
  b.toc[0].anchor = "";
  b.bytes[1] = 100;
  assert(!chaptertime::extent(b, 1, e));
  assert(!chaptertime::extent(b, -1, e));
  chaptertime::Extent huge{1, UINT64_MAX};
  pages = 42;
  assert(!chaptertime::remainingPages(huge, 1, 10, pages) && pages == 42);
  assert(!chaptertime::remainingPages({100, 10}, 0, 10, pages));
}
