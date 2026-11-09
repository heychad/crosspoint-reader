"""Compile the real page-shortcut branch and end-of-book dispatcher together."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
shortcuts = (root / 'src/activities/reader/EpubReaderShortcuts.cpp').read_text()
pages = 'case Action::PreviousPage:' + shortcuts.split('case Action::PreviousPage:', 1)[1].split('case Action::PreviousChapter:', 1)[0]
managed = 'bool EpubReaderActivity::customSideShortcuts()' + shortcuts.split('bool EpubReaderActivity::customSideShortcuts()', 1)[1].split('bool EpubReaderActivity::handleReaderShortcut', 1)[0]
reader = (root / 'src/activities/reader/ReaderActivity.cpp').read_text()
end = 'bool ReaderActivity::handleEndOfBookPageTurn(' + reader.split('bool ReaderActivity::handleEndOfBookPageTurn(', 1)[1].split('void ReaderActivity::pollSettingsSaveWarning()', 1)[0]
code = r'''
#include <atomic>
#include <cassert>
#include <memory>
#include "personalization/ReaderShortcuts.h"
using readershortcuts::Action;
struct Settings {
  int readerUpTap = 0, readerUpHold = 0, readerDownTap = 0, readerDownHold = 0, readerSideChord = 0;
} SETTINGS;
struct Options { bool menuActive() { return false; } };
class ReaderActivity {
 public:
  bool end = true, home = false;
  int returned = 0;
  std::atomic<bool> endOfBookOptionsReady{true};
  std::unique_ptr<Options> endOfBookOptions = std::make_unique<Options>();
  bool isAtEndOfBook() { return end; }
  void onGoHome() { home = true; }
  void onReturnFromEndOfBook() { end = false; ++returned; }
  void requestUpdate() {}
  bool handleEndOfBookPageTurn(bool prevTriggered, bool nextTriggered);
};
class EpubReaderActivity : public ReaderActivity {
 public:
  bool section = true, pendingManualTurnTouch = false;
  int pendingManualTurn = 0;
  bool customSideShortcuts() const;
  bool dispatch(Action action) {
    switch (action) {
''' + pages + r'''
      default: return false;
    }
  }
};
''' + managed + end + r'''
int main() {
  SETTINGS.readerUpHold = static_cast<int>(Action::Menu);
  for (bool defaults : {false, true}) {
    EpubReaderActivity previous;
    assert(previous.customSideShortcuts());
    auto prev = defaults ? readershortcuts::defaultAction(true, false, 0, false, 0) : Action::PreviousPage;
    assert(previous.dispatch(prev));
    assert(previous.returned == 1 && !previous.end && previous.pendingManualTurn == 0);
    EpubReaderActivity next;
    auto fwd = defaults ? readershortcuts::defaultAction(false, false, 0, false, 0) : Action::NextPage;
    assert(next.dispatch(fwd));
    assert(next.home && next.pendingManualTurn == 0);
  }
  EpubReaderActivity regular;
  regular.end = false;
  assert(regular.dispatch(Action::NextPage));
  assert(regular.pendingManualTurn == 1 && !regular.home);
}
'''
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p/'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-I', str(root/'src'), str(p/'test.cpp'), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
print('End-of-book shortcut checks passed')
