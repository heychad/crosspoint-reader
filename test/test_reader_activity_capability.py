"""Exercise production manager routing for reader-context children and real readers."""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
manager = (ROOT / "src/activities/ActivityManager.cpp").read_text()
activity = (ROOT / "src/activities/Activity.h").read_text()
reader = (ROOT / "src/activities/reader/ReaderActivity.h").read_text()
clip = (ROOT / "src/activities/reader/ClipSelectionActivity.h").read_text()
interval = (ROOT / "src/activities/util/IntervalSelectionActivity.h").read_text()


def method(signature):
    start = manager.index(signature)
    opening = manager.index("{", start)
    depth = 0
    for end in range(opening, len(manager)):
        depth += (manager[end] == "{") - (manager[end] == "}")
        if depth == 0:
            return manager[start:end + 1]
    raise ValueError(signature)


def declaration(header, name):
    return next((line.strip() for line in header.splitlines() if name in line), "")


route = manager.split("  bool readerGestureHandled = false;", 1)[1].split(
    "  if (currentActivity && !readerGestureHandled)", 1
)[0]
push = manager.split("} else if (pendingAction == PendingAction::Push) {", 1)[1].split(
    "        // Move current activity", 1
)[0]
program = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
class ReaderActivity;
enum class HomeButtonAction { Ignore, QuickActions, Home };
namespace readershortcuts { enum class Action { QuickActions }; }
struct Input { HomeButtonAction action=HomeButtonAction::Ignore;HomeButtonAction homeButtonAction()const{return action;} };
class Activity {
 public:
  virtual ~Activity()=default;
''' + declaration(activity, "virtual bool isReaderActivity()") + "\n" + declaration(activity, "asReaderActivity()") + r'''
};
class ReaderActivity: public Activity {
 public:
  int gestures=0,finishes=0,shortcuts=0;
  struct Stats {int pauses=0;void pause(){++pauses;}}readingStats;
  bool handleReaderGestureInput(){++gestures;return true;}
  void finishReaderGestures(){++finishes;}
  void handleReaderShortcut(readershortcuts::Action){++shortcuts;}
''' + declaration(reader, "bool isReaderActivity()") + "\n" + declaration(reader, "asReaderActivity()") + r'''
};
class ClipSelectionActivity: public Activity {
 public:
  std::array<uint32_t,8> pageStorage{0x11223344,0x55667788,0xaabbccdd};
''' + declaration(clip, "bool isReaderActivity()") + r'''
};
class IntervalSelectionActivity: public Activity {
 public:
  bool readerActivity=true;
  std::array<uint32_t,8> selectionStorage{0x77889900,0x11223344};
''' + declaration(interval, "bool isReaderActivity()") + r'''
};
class ActivityManager {
 public:
  enum class PendingAction { None, Push };
  PendingAction pendingAction=PendingAction::None;
  std::unique_ptr<Activity>currentActivity;
  std::vector<std::unique_ptr<Activity>>stackActivities;
  Input mappedInput;
  bool isForegroundReader()const;bool isReaderActivity()const;
  bool route(){bool readerGestureHandled=false;
''' + route + r'''
   return readerGestureHandled;
  }
  void pauseForPush(){
''' + push + r'''
  }
};
''' + method("bool ActivityManager::isForegroundReader()") + "\n" + method("bool ActivityManager::isReaderActivity()") + r'''
int main(){
 ActivityManager manager;assert(!manager.route()&&!manager.isForegroundReader());
 auto clip=std::make_unique<ClipSelectionActivity>();auto*clipPtr=clip.get();const auto pages=clipPtr->pageStorage;
 manager.currentActivity=std::move(clip);
 for(auto action:{HomeButtonAction::Ignore,HomeButtonAction::QuickActions,HomeButtonAction::Home}){
  manager.mappedInput.action=action;bool handled=manager.route();manager.pauseForPush();
  assert(clipPtr->pageStorage==pages);assert(!handled&&!manager.isForegroundReader());assert(manager.isReaderActivity());
 }
 auto interval=std::make_unique<IntervalSelectionActivity>();auto*intervalPtr=interval.get();const auto selection=intervalPtr->selectionStorage;
 manager.currentActivity=std::move(interval);
 for(bool readerContext:{false,true}){
  intervalPtr->readerActivity=readerContext;
  for(auto action:{HomeButtonAction::Ignore,HomeButtonAction::QuickActions,HomeButtonAction::Home}){
   manager.mappedInput.action=action;assert(!manager.route());manager.pauseForPush();
   assert(intervalPtr->selectionStorage==selection&&!manager.isForegroundReader());
   assert(manager.isReaderActivity()==readerContext);
  }
 }
 auto reader=std::make_unique<ReaderActivity>();auto*r=reader.get();manager.currentActivity=std::move(reader);
 manager.mappedInput.action=HomeButtonAction::Ignore;assert(manager.route()&&r->gestures==1);
 manager.mappedInput.action=HomeButtonAction::QuickActions;assert(manager.route()&&r->finishes==1&&r->shortcuts==1);
 manager.mappedInput.action=HomeButtonAction::Home;assert(!manager.route()&&r->finishes==2);
 manager.pauseForPush();assert(r->readingStats.pauses==1&&manager.isForegroundReader());
 manager.pendingAction=ActivityManager::PendingAction::Push;assert(!manager.isForegroundReader());
}
'''

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    cpp = path / "routing.cpp"
    executable = path / "routing"
    cpp.write_text(program)
    subprocess.run([
        "c++", "-std=c++20", "-g", "-fsanitize=address,undefined",
        "-fno-sanitize-recover=all", str(cpp), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)

print("Manager routes reader state only to ReaderActivity; clipping and interval storage remain intact")
