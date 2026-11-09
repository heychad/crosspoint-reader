#include <EpdFont.h>
#include <FreeInkUICore.h>
#include <Utf8.h>
#include <builtinFonts/ubuntu_10_regular.h>
#include <builtinFonts/ubuntu_12_regular.h>
#include <components/media/cover-grid.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace fui = freeink::ui;
void (*layoutProbe)() = nullptr;
bool trackAllocations = false;
size_t allocations = 0;
void* operator new(size_t size) {
  if (trackAllocations) ++allocations;
  if (void* data = std::malloc(size == 0 ? 1 : size)) return data;
  throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* data) noexcept { std::free(data); }
void operator delete[](void* data) noexcept { std::free(data); }
#define LOG_ERR(...) ((void)0)
struct GfxRenderer {};
int cacheStarts = 0;
int cacheDraws = 0;
int parserCalls = 0;
bool failAllocation = false;
bool failGeneration = false;
bool oversizedThumb = false;
struct StorageModel {
  bool exists(const char*) { return false; }
} Storage;
struct HomeCoverCache {
  static constexpr int MAX_COVERS = 7;
  explicit HomeCoverCache(GfxRenderer&) {}
  void begin() { ++cacheStarts; }
  void invalidate() {}
  void invalidate(int) {}
  void prepare() {}
  bool paint(fui::Rect, int slot, const char*) {
    assert(slot >= 0 && slot < MAX_COVERS);
    ++cacheDraws;
    return true;
  }
};
class Target final : public fui::DrawTarget {
 public:
  size_t largestTitle = 0;
  fui::Size measureText(fui::FontId font, const char* text, fui::TextStyle) const override {
    int width = 0, height = 0;
    (font == 0 ? small : body).getTextDimensions(text, &width, &height);
    return {static_cast<int16_t>(width), lineHeight(font)};
  }
  int16_t lineHeight(fui::FontId font) const override {
    if (layoutProbe) layoutProbe();
    return font == 0 ? ubuntu_10_regular.advanceY : ubuntu_12_regular.advanceY;
  }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect rect, const char* text, fui::TextStyle style) override {
    largestTitle = std::max(largestTitle, std::strlen(text));
    fui::layoutText(*this, rect, text, style, [rect](const char*, fui::Rect line) {
      assert(line.x >= rect.x && line.right() <= rect.right());
      assert(line.y >= rect.y && line.bottom() <= rect.bottom());
    });
  }
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}

 private:
  EpdFont small{&ubuntu_10_regular};
  EpdFont body{&ubuntu_12_regular};
};
struct UiAppHost {
  struct UiScreen {
    struct Theme {
      fui::TextStyle smallText{};
      int16_t spaceSm = 6;
    } themeValues;
    Target& output;
    fui::Frame<24>& frameValue;
    fui::Rect area;
    fui::Rect body() const { return area; }
    Theme& theme() { return themeValues; }
    Target& target() { return output; }
    fui::Frame<24>& frame() { return frameValue; }
  };
};
namespace FsHelpers {
bool hasReflowableBookExtension(const std::string& path) { return path.ends_with(".epub") || path.ends_with(".txt"); }
bool hasXtcExtension(const std::string& path) { return path.ends_with(".xtc"); }
}  // namespace FsHelpers
template <typename T, typename... Args>
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  if (failAllocation) return {};
  return std::make_unique<T>(std::forward<Args>(args)...);
}
struct Epub {
  Epub(const std::string&, const char*) { ++parserCalls; }
  bool generateThumbBmpFromSource(int height) {
    assert(height > 0);
    return !failGeneration;
  }
  std::string getThumbBmpPath(int) const {
    return oversizedThumb ? std::string(200, 'x') : "/.crosspoint/epub/thumbnail.bmp";
  }
};
struct Xtc {
  Xtc(const std::string&, const char*) { ++parserCalls; }
  bool load() { return !failGeneration; }
  bool generateThumbBmp(int height) {
    assert(height > 0);
    return !failGeneration;
  }
  std::string getThumbBmpPath(int) const { return "/.crosspoint/xtc/thumbnail.bmp"; }
};
// INSERT_GRID_HEADER
// INSERT_GRID_SOURCE

LibraryCoverGrid* observedGrid = nullptr;
int observedPage = -1;
int main() {
  GfxRenderer renderer;
  LibraryCoverGrid grid(renderer);
  assert(cacheStarts == 1);
  for (const auto size : {fui::Size{480, 800}, fui::Size{800, 480}, fui::Size{528, 792}, fui::Size{792, 528}}) {
    for (int font : {0, 1}) {
      Target target;
      fui::DeviceContext device;
      device.width = size.width;
      device.height = size.height;
      fui::InteractionBuffer<24> interactions;
      fui::Frame<24> frame(target, device, {}, interactions);
      UiAppHost::UiScreen screen{{}, target, frame, {0, 100, size.width, static_cast<int16_t>(size.height - 160)}};
      screen.themeValues.smallText.font = font;
      assert(grid.layout(screen));
      assert(grid.pageSize() == 6);
      for (int i = 0; i < 6; ++i) grid.setBook(i, i + 1, "A readable book title");
      grid.draw(screen, 12, 6, 14, 1);
      assert(interactions.count() == 6);
      for (int i = 0; i < 6; ++i) {
        const auto& hit = interactions.data()[i];
        assert(hit.value == i + 12);
        assert(hit.rect.x >= 0 && hit.rect.right() <= size.width);
        assert(hit.rect.y >= 100 && hit.rect.bottom() <= size.height - 60);
      }
    }
  }
  Target target;
  fui::DeviceContext device;
  device.width = 480;
  device.height = 800;
  fui::InteractionBuffer<24> interactions;
  fui::Frame<24> frame(target, device, {}, interactions);
  UiAppHost::UiScreen screen{{}, target, frame, {0, 100, 480, 640}};
  assert(grid.layout(screen));
  observedGrid = &grid;
  layoutProbe = [] { observedPage = observedGrid->pageSize(); };
  assert(grid.layout(screen));
  layoutProbe = nullptr;
  assert(observedPage > 0 && "a repaint published a transient zero page size");
  for (int i = 0; i < 6; ++i) grid.setBook(i, i + 1, "Book");
  assert(grid.nextCover() == 0);
  assert(grid.loadCover(0, "/a.epub"));
  assert(grid.nextCover() == 1);
  failGeneration = true;
  assert(!grid.loadCover(1, "/b.epub"));
  assert(grid.nextCover() == 2);
  failGeneration = false;
  failAllocation = true;
  assert(!grid.loadCover(2, "/c.epub"));
  assert(grid.nextCover() == 3);
  failAllocation = false;
  assert(!grid.loadCover(3, "/d.pdf"));
  assert(grid.loadCover(4, "/e.xtc"));
  grid.clearBook(5);
  assert(grid.nextCover() == -1);
  assert(grid.matches(0, 1) && !grid.matches(0, 77));
  grid.setBook(0, 77, "Replacement");
  assert(grid.nextCover() == 0);
  assert(!grid.matches(0, 1));
  oversizedThumb = true;
  assert(!grid.loadCover(4, "/oversized.epub"));
  oversizedThumb = false;
  grid.invalidate();
  assert(grid.nextCover() == 0);

  std::string longTitle;
  for (int i = 0; i < 400; ++i) longTitle += "é";
  for (int i = 0; i < 6; ++i) grid.setBook(i, i + 10, longTitle);
  grid.draw(screen, 0, 6, 0, 1);
  assert(target.largestTitle <= 512);
  trackAllocations = true;
  for (int pass = 0; pass < 100; ++pass) {
    interactions.clear();
    assert(grid.layout(screen));
    for (int i = 0; i < 6; ++i) grid.setBook(i, i + 10, longTitle);
    grid.draw(screen, 0, 6, pass % 6, 1);
  }
  trackAllocations = false;
  assert(allocations == 0);
  screen.area.height = 0;
  assert(!grid.layout(screen));
  assert(grid.pageSize() == 0);
}
