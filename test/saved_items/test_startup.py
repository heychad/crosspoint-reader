"""Exercise the saved-content handoff before the first reader frame."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text()


def method(signature):
    start = source.index(signature)
    end = source.index("\n}\n", start) + 2
    return source[start:end]


program = r"""
#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include "activities/ActivityResult.h"

#define LOG_ERR(...)
enum class SavedItemKind { Bookmarks, CrossInkBookmarks, Clippings };
struct RenderLock {
  RenderLock() = default;
  explicit RenderLock(const auto&) {}
};
struct Activity {
  virtual ~Activity() = default;
};
struct EpubReaderBookmarksActivity : Activity {
  explicit EpubReaderBookmarksActivity(auto&...) {}
};
struct CrossInkBookmarksActivity : Activity {
  explicit CrossInkBookmarksActivity(auto&...) {}
};
struct EpubReaderClippingListActivity : Activity {
  explicit EpubReaderClippingListActivity(auto&...) {}
};
bool failAllocation = false;
template <typename T, typename... Args>
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  return failAllocation ? nullptr : std::make_unique<T>(std::forward<Args>(args)...);
}
struct {
  bool present = true;
  bool hasClippings() const { return present; }
} CLIPPINGS;
struct Epub {
  const std::string path = "/Books/fixture.epub";
  const std::string& getPath() const { return path; }
};
struct ReaderActivity {
  int savedPage = 7, pageOnScreen = -1;
  bool trackingStarted = false, remembered = false;
  unsigned updates = 0;
  void onEnter() { trackingStarted = true; requestUpdate(); }
  void rememberBook() { assert(trackingStarted && !remembered); remembered = true; }
  void requestUpdate() { ++updates; }
  void render(RenderLock&&) { pageOnScreen = savedPage; }
};
struct EpubReaderActivity : ReaderActivity {
  struct { void clearScreen() {} } renderer;
  int mappedInput = 0;
  struct {
    bool active = false, shown = false;
    bool isActive() const { return active; }
    void processRender(auto&, int&) { shown = true; }
  } loadFailurePopup;
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::optional<SavedItemKind> pendingSavedView;
  std::unique_ptr<Activity> child;
  std::function<void(const ActivityResult&)> response;
  int bookmarkReads = 0;
  void onEnter();
  void openSavedView(SavedItemKind kind);
  void resumeFromSavedView();
  void render(RenderLock&& lock);
  void loadCachedBookmarks() { ++bookmarkReads; }
  void applyProgressChange(const ProgressChangeResult& progress) {
    render(RenderLock{});
    assert(pageOnScreen < 0);  // A stale refresh must not paint before the jump is applied.
    savedPage = progress.page;
  }
  void startActivityForResult(std::unique_ptr<Activity> list, auto callback) {
    assert(trackingStarted);
    child = std::move(list);
    response = std::move(callback);
  }
};
""" + "\n".join(
    method(signature) for signature in (
        "void EpubReaderActivity::onEnter()",
        "void EpubReaderActivity::openSavedView(",
        "void EpubReaderActivity::resumeFromSavedView()",
        "void EpubReaderActivity::render(RenderLock&& lock)",
    )
) + r"""
int main() {
  for (auto kind : {SavedItemKind::Bookmarks, SavedItemKind::CrossInkBookmarks, SavedItemKind::Clippings}) {
    EpubReaderActivity reader;
    reader.pendingSavedView = kind;
    reader.render(RenderLock{});
    assert(reader.pageOnScreen < 0);
    reader.onEnter();
    assert(reader.child && reader.remembered && reader.trackingStarted && reader.pendingSavedView);
    reader.render(RenderLock{});
    assert(reader.pageOnScreen < 0);
    ActivityResult result;
    ProgressChangeResult target;
    target.spineIndex = 2;
    target.page = 3;
    result.data = target;
    reader.response(result);
    assert(!reader.pendingSavedView && reader.savedPage == 3 && reader.pageOnScreen < 0);
    assert(reader.bookmarkReads == (kind == SavedItemKind::Clippings ? 0 : 1));
    reader.render(RenderLock{});
    assert(reader.pageOnScreen == 3);
  }
  EpubReaderActivity cancel;
  cancel.pendingSavedView = SavedItemKind::Clippings;
  cancel.onEnter();
  ActivityResult cancelled;
  cancelled.isCancelled = true;
  cancel.response(cancelled);
  cancel.render(RenderLock{});
  assert(cancel.pageOnScreen == 7 && cancel.remembered);
  for (bool clipsPresent : {false, true}) {
    CLIPPINGS.present = clipsPresent;
    failAllocation = clipsPresent;
    EpubReaderActivity fallback;
    fallback.pendingSavedView = SavedItemKind::Clippings;
    fallback.onEnter();
    assert(!fallback.child && !fallback.pendingSavedView && !fallback.remembered);
    fallback.render(RenderLock{});
    assert(fallback.pageOnScreen == 7 && fallback.updates > 0);
  }
  EpubReaderActivity unreadable;
  unreadable.pendingSavedView = SavedItemKind::Clippings;
  unreadable.epub.reset();
  unreadable.loadFailurePopup.active = true;
  unreadable.onEnter();
  unreadable.render(RenderLock{});
  assert(!unreadable.child && !unreadable.remembered && unreadable.loadFailurePopup.shown);
  EpubReaderActivity normal;
  normal.onEnter();
  normal.render(RenderLock{});
  assert(!normal.child && normal.pageOnScreen == 7);
  failAllocation = false;
  normal.openSavedView(SavedItemKind::Clippings);
  assert(normal.child && !normal.pendingSavedView);
  normal.response(cancelled);
  assert(normal.savedPage == 7 && normal.bookmarkReads == 0);
}
"""

with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "saved-startup.cpp"
    binary = Path(directory) / "saved-startup"
    cpp.write_text(program)
    subprocess.run(
        [os.environ.get("CXX", "c++"), "-std=c++20", *shlex.split(os.environ.get("CXXFLAGS", "")),
         "-I", str(ROOT / "src"), str(cpp), "-o", str(binary)],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
