"""Reader-context overlays must not be treated as ReaderActivity objects."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/ActivityManager.cpp').read_text()
activity = (root / 'src/activities/Activity.h').read_text()
reader = (root / 'src/activities/reader/ReaderActivity.h').read_text()
clip = (root / 'src/activities/reader/ClipSelectionActivity.h').read_text()
interval = (root / 'src/activities/util/IntervalSelectionActivity.h').read_text()
assert 'virtual ReaderActivity* bookReader() { return nullptr; }' in activity
assert 'ReaderActivity* bookReader() final { return this; }' in reader
assert 'isReaderActivity() const override' in clip and 'bookReader()' not in clip
assert 'isReaderActivity() const override' in interval and 'bookReader()' not in interval
start = source.index('bool ActivityManager::isForegroundReader() const {')
end = source.index('\nbool ActivityManager::isForegroundReaderPage()', start)
predicate = source[start:end]
assert 'static_cast<ReaderActivity*>' not in source
assert source.count('currentActivity ? currentActivity->bookReader() : nullptr') >= 2
program = r'''
#include <cassert>
#include <memory>
struct ReaderActivity;
struct Activity {
  virtual ~Activity() = default;
  virtual bool isReaderActivity() const { return false; }
  virtual ReaderActivity* bookReader() { return nullptr; }
};
struct ReaderActivity : Activity {
  bool isReaderActivity() const override { return true; }
  ReaderActivity* bookReader() override { return this; }
};
struct ReaderOverlay : Activity {
  bool isReaderActivity() const override { return true; }
};
struct ActivityManager {
  enum class PendingAction { None, Pop };
  PendingAction pendingAction = PendingAction::None;
  std::unique_ptr<Activity> currentActivity;
  bool isForegroundReader() const;
};
''' + predicate + r'''
int main() {
  ActivityManager manager;
  assert(!manager.isForegroundReader());
  manager.currentActivity = std::make_unique<ReaderOverlay>();
  assert(manager.currentActivity->isReaderActivity());
  assert(!manager.isForegroundReader());
  manager.currentActivity = std::make_unique<ReaderActivity>();
  assert(manager.isForegroundReader());
  manager.pendingAction = ActivityManager::PendingAction::Pop;
  assert(!manager.isForegroundReader());
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++20', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Reader overlays remain distinct from book-reader objects')
