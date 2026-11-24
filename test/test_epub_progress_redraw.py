"""Exercise the completed EPUB render's progress writes during partial indexing and reflow."""
from pathlib import Path
import subprocess
import tempfile


root = Path(__file__).resolve().parents[1]
reader = (root / "src/activities/reader/EpubReaderActivity.cpp").read_text()
render = reader.split("void EpubReaderActivity::renderBook()", 1)[1]
after_page = render.index("    markPageRendered();")
start = render.index("  const int totalPages = section->estimatedTotalPages();", after_page)
end = render.index("  showPendingSyncSaveError();", start)
harness = r"""
#include <cassert>
#include <cstdint>
#include <optional>
#include <tuple>
#include <vector>

int main() {
  struct Section {
    int currentPage = 3, pageCount = 8, estimate = 50;
    int estimatedTotalPages() const { return estimate; }
  } partial;
  auto* section = &partial;
  int currentSpineIndex = 1, lastSavedSpineIndex = -1, lastSavedPage = -1, lastSavedPageCount = -1;
  std::optional<uint32_t> currentPageVisibleOffset = 400, lastSavedVisibleOffset;
  using Position = std::tuple<int, int, int, uint32_t>;
  std::vector<Position> attempts;
  bool writable = true;
  auto saveProgress = [&](int spine, int page, int count) {
    attempts.emplace_back(spine, page, count, currentPageVisibleOffset.value());
    return writable;
  };
  auto redraw = [&] {
""" + render[start:end] + r"""
  };
  for (int i = 0; i < 100; ++i) redraw();
  assert(attempts.size() == 1 && attempts.back() == Position(1, 3, 50, 400));
  section->pageCount = 50;
  section->estimate = 72;
  redraw();
  assert(attempts.size() == 2 && attempts.back() == Position(1, 3, 72, 400));
  section->pageCount = 72;
  redraw();
  assert(attempts.size() == 2);
  currentPageVisibleOffset = 612;
  redraw();
  assert(attempts.size() == 3 && attempts.back() == Position(1, 3, 72, 612));
  ++section->currentPage;
  writable = false;
  redraw();
  writable = true;
  redraw();
  redraw();
  assert(attempts.size() == 5 && attempts[3] == attempts[4] && attempts.back() == Position(1, 4, 72, 612));
  ++currentSpineIndex;
  redraw();
  assert(attempts.size() == 6 && attempts.back() == Position(2, 4, 72, 612));
}
"""
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / "progress.cpp"
    binary = source.with_suffix("")
    source.write_text(harness)
    subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
