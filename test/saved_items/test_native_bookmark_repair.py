"""Run the bookmark-list recovery callbacks from the real reader activity."""

from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[2] / "src/activities/reader/EpubReaderBookmarksActivity.cpp").read_text()


def methods(start, end):
    return source[source.index(start):source.index(end)]


code = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <string>
#include <utility>
#include <vector>

#define tr(id) #id
#define LOG_ERR(...) ((void)0)
bool renderLocked = false;
struct RenderLock {
  explicit RenderLock(const auto&) { assert(!renderLocked); renderLocked = true; }
  ~RenderLock() { renderLocked = false; }
};
struct ActivityResult { bool isCancelled = false; };
struct Popup {
  bool deliveringConfirmation = false;
  int notices = 0;
  void showMessage(const char*, const char*, const char* const*, int, int, const auto&) {
    assert(renderLocked && !deliveringConfirmation);
    ++notices;
  }
  bool handleInput(int, const auto&) { return false; }
};
namespace BookmarkFile {
enum class LoadResult { Unavailable };
bool save(const std::string&, const std::vector<int>& entries) {
  assert(renderLocked && entries == std::vector<int>{2});
  return false;
}
LoadResult load(const std::string&, std::vector<int>& entries) {
  assert(renderLocked && entries == (std::vector<int>{1, 2}));
  return LoadResult::Unavailable;
}
}
class EpubReaderBookmarksActivity {
 public:
  struct {
    std::atomic<int> selected = 0;
    void follow(int) {}
  } nav;
  std::string epubPath = "test.epub";
  std::vector<int> bookmarks{1, 2};
  bool bookmarksReadable = true, saveNoticePending = false, confirmingDelete = false;
  Popup confirmPopup;
  int mappedInput = 0, rebuilt = 0;
  int listCount() const { return static_cast<int>(bookmarks.size()); }
  bool handleCustomInput();
  void deleteSelectedBookmark();
  void recoverAfterSaveFailure();
  void rebuildBookmarkRowItems() { assert(renderLocked); ++rebuilt; }
  void requestUpdate(bool = false) {}
  void setResult(ActivityResult) { assert(false); }
  void finish() { assert(false); }
};
''' + methods(
    "bool EpubReaderBookmarksActivity::handleCustomInput(",
    "bool EpubReaderBookmarksActivity::handleButtons(",
) + methods(
    "void EpubReaderBookmarksActivity::deleteSelectedBookmark(",
    "void EpubReaderBookmarksActivity::buildScreen(",
) + r'''
int main() {
  EpubReaderBookmarksActivity activity;
  activity.confirmPopup.deliveringConfirmation = true;
  activity.deleteSelectedBookmark();
  assert(!activity.bookmarksReadable && activity.rebuilt == 2);
  assert(activity.confirmPopup.notices == 0 && activity.saveNoticePending);
  activity.confirmPopup.deliveringConfirmation = false;
  assert(activity.handleCustomInput());
  assert(activity.confirmPopup.notices == 1 && !activity.saveNoticePending);
  assert(!activity.handleCustomInput() && !renderLocked);
}
'''

with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    (temp / "repair.cpp").write_text(code)
    subprocess.run(["c++", "-std=c++20", str(temp / "repair.cpp"), "-o", str(temp / "repair")], check=True)
    subprocess.run([str(temp / "repair")], check=True)
