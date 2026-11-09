"""Exercise production tab changes while a renderer reads the model."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/library/LibraryListActivity.cpp').read_text()
start = source.index('void LibraryListActivity::selectTab(')
end = source.index('void LibraryListActivity::toggleSortDirection(', start)
body = source[start:end]
code = r'''
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#define FREEINK_CAP_TOUCH 0
std::mutex renderMutex;
class LibraryListActivity;
struct RenderLock {
  std::unique_lock<std::mutex> lock;
  explicit RenderLock(LibraryListActivity&) : lock(renderMutex) {}
};
int orderForTab(int index, uint8_t) { return index; }
struct Nav { int selected = 0, top = 0; };
class LibraryListActivity {
 public:
  static constexpr int TAB_SLOTS = 5;
  int activeTabIndex = 0, sortOrder = 0, filteredOrder = 0;
  uint8_t descendingTabs = 0;
  Nav nav;
  int activeTab() const { return activeTabIndex; }
  Nav& activeNav() { return nav; }
  void applyFilter() { filteredOrder = sortOrder; std::this_thread::sleep_for(std::chrono::microseconds(30)); }
  void refreshOverlap() {}
  void requestUpdate() {}
  void selectTab(int index, bool toggleIfActive);
};
''' + body + r'''
int main() {
  LibraryListActivity library;
  std::atomic<bool> finished{false}, inconsistent{false};
  std::thread renderer([&] {
    while (!finished.load()) {
      std::lock_guard<std::mutex> lock(renderMutex);
      if (library.activeTabIndex != library.sortOrder || library.filteredOrder != library.sortOrder)
        inconsistent.store(true);
    }
  });
  for (int i = 0; i < 200; ++i) library.selectTab(i % 5, false);
  finished.store(true);
  renderer.join();
  return inconsistent.load() ? 1 : 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-pthread', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Library tab/render locking checks passed')
