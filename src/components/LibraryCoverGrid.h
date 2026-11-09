#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

#include "HomeCoverCache.h"
#include "UiAppHost.h"
#include "components/media/cover-grid.h"

// One page of Library covers. Book actions and ordering belong to the Library.
class LibraryCoverGrid {
 public:
  static constexpr int MAX_BOOKS = 6;
  explicit LibraryCoverGrid(GfxRenderer& renderer);
  bool layout(UiAppHost::UiScreen& screen);
  void setBook(int slot, uint64_t identity, const std::string& title);
  void clearBook(int slot);
  void draw(UiAppHost::UiScreen& screen, int firstEntry, int count, int selected, freeink::ui::ActionId action);
  int pageSize() const { return visibleBooks.load(); }
  int nextCover() const;
  bool matches(int slot, uint64_t identity) const;
  bool loadCover(int slot, const std::string& path);
  void invalidate();

 private:
  struct Book {
    uint64_t identity = 0;
    std::array<char, 513> title{};
    std::array<char, 128> coverPath{};
    bool present = false;
    bool attempted = false;
  };
  HomeCoverCache coverCache;
  std::array<Book, MAX_BOOKS> books;
  freeink::ui::CoverGridProps props;
  freeink::ui::Rect bounds{};
  std::atomic<int> visibleBooks{0};
  int coverHeight = 0;
  int firstEntry = 0;
};
