"""Exercise the firmware's main-task identity check with two task handles."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
header = (root / 'src/activities/ActivityManager.h').read_text()
source = (root / 'src/activities/ActivityManager.cpp').read_text()
start = header.index('  bool isMainTask() const {')
end = header.index('\n  }', start) + len('\n  }')
method = header[start:end]
begin = source.index('void ActivityManager::begin() {')
register = source[begin:source.index('#if defined(configNUM_CORES)', begin)]
assert 'mainTaskHandle.compare_exchange_strong' in register
register = register[register.index('  TaskHandle_t noTask'):]
code = r'''
#include <atomic>
#include <cassert>
using TaskHandle_t = void*;
TaskHandle_t current = nullptr;
TaskHandle_t xTaskGetCurrentTaskHandle() { return current; }
struct Manager {
  std::atomic<TaskHandle_t> mainTaskHandle{nullptr};
''' + method + r'''
  void begin() {
''' + register + r'''
  }
};
int main() {
  int mainTask = 0, renderTask = 0;
  Manager manager;
  current = &mainTask;
  assert(!manager.isMainTask());
  manager.begin();
  assert(manager.isMainTask());
  current = &renderTask;
  assert(!manager.isMainTask());
  manager.begin();
  assert(!manager.isMainTask());
  current = nullptr;
  assert(!manager.isMainTask());
  current = &mainTask;
  assert(manager.isMainTask());
}
'''
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Main-task identity and uninitialized guard checks passed')
