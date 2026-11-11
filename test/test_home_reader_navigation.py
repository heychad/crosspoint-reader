"""Exercise the production Home/Reader stack-navigation decisions."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/ActivityManager.cpp').read_text()
start = source.index('bool ActivityManager::handleHomeReaderShortcut()')
methods = source[start:]
assert 'if (pendingAction != PendingAction::None)\n            pendingHomeReaderTarget = nullptr;' in source
program = r'''
#include <algorithm>
#include <cassert>
#include <memory>
#include <utility>
#include <vector>
struct ActivityResult { bool isCancelled = false; };
struct Activity {
  bool home = false, reader = false, exclusive = false, cancelled = false;
  bool isHomeActivity() const { return home; }
  bool requiresExclusiveStorageLoop() const { return exclusive; }
  Activity* bookReader() { return reader ? this : nullptr; }
  void setResult(ActivityResult&& result) { cancelled = result.isCancelled; }
};
struct ActivityManager {
  enum class PendingAction { None, Pop, Replace };
  PendingAction pendingAction = PendingAction::None;
  Activity* pendingHomeReaderTarget = nullptr;
  std::unique_ptr<Activity> currentActivity;
  std::vector<std::unique_ptr<Activity>> stackActivities;
  bool wentHome = false;
  void goHome() { wentHome = true; pendingAction = PendingAction::Replace; }
  void popActivity() { pendingAction = PendingAction::Pop; }
  bool handleHomeReaderShortcut();
  bool continueHomeReaderUnwind();
};
''' + methods + r'''
int main() {
  ActivityManager m;
  assert(m.handleHomeReaderShortcut() && !m.wentHome);
  m.currentActivity = std::make_unique<Activity>();
  m.currentActivity->exclusive = true;
  assert(m.handleHomeReaderShortcut() && !m.wentHome);
  m.currentActivity->exclusive = false;
  assert(m.handleHomeReaderShortcut() && m.wentHome);
  m.wentHome = false; m.pendingAction = ActivityManager::PendingAction::None;
  m.currentActivity->reader = true;
  assert(m.handleHomeReaderShortcut() && m.wentHome);
  m.wentHome = false; m.pendingAction = ActivityManager::PendingAction::None;
  auto reader = std::move(m.currentActivity);
  Activity* target = reader.get();
  m.stackActivities.push_back(std::move(reader));
  m.stackActivities.push_back(std::make_unique<Activity>());
  m.currentActivity = std::make_unique<Activity>();
  assert(m.handleHomeReaderShortcut());
  assert(m.currentActivity->cancelled && m.pendingHomeReaderTarget == target);
  assert(m.pendingAction == ActivityManager::PendingAction::Pop);
  m.currentActivity = std::move(m.stackActivities.back()); m.stackActivities.pop_back();
  m.pendingAction = ActivityManager::PendingAction::None;
  assert(m.continueHomeReaderUnwind() && m.currentActivity->cancelled);
  m.currentActivity = std::move(m.stackActivities.back()); m.stackActivities.pop_back();
  m.pendingAction = ActivityManager::PendingAction::None;
  assert(!m.continueHomeReaderUnwind() && !m.pendingHomeReaderTarget && m.currentActivity.get() == target);
  m.stackActivities.push_back(std::move(m.currentActivity));
  m.pendingHomeReaderTarget = m.stackActivities.back().get();
  m.currentActivity = std::make_unique<Activity>(); m.currentActivity->exclusive = true;
  assert(!m.continueHomeReaderUnwind() && !m.pendingHomeReaderTarget);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++20', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Home/Reader cancels panels and returns to the existing reader')
