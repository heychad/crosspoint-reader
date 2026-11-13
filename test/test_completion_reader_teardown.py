"""Exercise the production replacement branch before a Home completion attempt."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/activities/ActivityManager.cpp').read_text()


def body(signature):
    start = source.index(signature)
    begin = source.index('{', start)
    end, depth = begin + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin + 1:end - 1]


replacement = body('else if (pendingActivity)')
exit_body = body('void ActivityManager::exitActivity(')
program = r'''
#include <cassert>
#include <memory>
#include <string>
#include <vector>
#define LOG_DBG(...) ((void)0)
bool held = false;
int files = 0, leases = 0, entries = 0;
std::vector<std::string> events;
struct RenderLock {
  bool locked = true;
  RenderLock() { assert(!held); held = true; }
  void unlock() { assert(held && locked); held = false; locked = false; }
  ~RenderLock() { if (locked) held = false; }
};
struct HeaderBackTapTarget { static void clear() { assert(held); } };
struct ReaderProbe { struct { void pause() {} } readingStats; void finishReaderGestures() {} };
struct Activity {
  virtual ~Activity() = default;
  virtual void onExit() { assert(held); }
  virtual void onEnter() {}
  virtual ReaderProbe* bookReader() { return nullptr; }
};
struct Reader final : Activity {
  Reader() { ++files; ++leases; }
  void onExit() override { assert(held); events.emplace_back("flush"); }
  ~Reader() override { assert(held); --files; --leases; events.emplace_back("reader destroyed"); }
};
struct Panel final : Activity {
  ~Panel() override { assert(held); events.emplace_back("panel destroyed"); }
};
struct Home final : Activity {
  void onEnter() override {
    assert(!held && files == 0 && leases == 0);
    ++entries; events.emplace_back("completion may start");
  }
};
struct ActivityManager {
  enum class PendingAction {None, Replace, ReplaceForSleep, Push};
  PendingAction pendingAction = PendingAction::Replace;
  std::unique_ptr<Activity> currentActivity, pendingActivity;
  std::vector<std::unique_ptr<Activity>> stackActivities;
  unsigned screenVisit = 0;
  void exitActivity(const RenderLock& lock) {
''' + exit_body + r'''
  }
  void processReplacement() { while (pendingActivity) {
''' + replacement + r'''
  } }
};
int main() {
  // Going Home directly from the reader destroys its owners before entering Home.
  ActivityManager direct;
  direct.currentActivity = std::make_unique<Reader>();
  direct.pendingActivity = std::make_unique<Home>();
  direct.processReplacement();
  assert(entries == 1 && direct.stackActivities.empty() && !held);
  assert((events == std::vector<std::string>{"flush", "reader destroyed", "completion may start"}));
  direct.currentActivity.reset();
  events.clear();
  // Going Home while a child screen is open must also destroy the stacked reader.
  ActivityManager nested;
  nested.currentActivity = std::make_unique<Panel>();
  nested.stackActivities.push_back(std::make_unique<Reader>());
  nested.pendingActivity = std::make_unique<Home>();
  nested.processReplacement();
  assert(entries == 2 && nested.stackActivities.empty() && !held);
  assert((events == std::vector<std::string>{"panel destroyed", "flush", "reader destroyed", "completion may start"}));
  nested.currentActivity.reset();
}
'''
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    src, binary = directory / 'test.cpp', directory / 'test'
    src.write_text(program)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), str(src), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Production Home replacement branch destroys current and stacked readers before onEnter')
