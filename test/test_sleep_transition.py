"""Exercise real manager transitions with stale input and exclusive USB storage."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
path = 'src/activities/ActivityManager.cpp'
revision = os.environ.get('SLEEP_TRANSITION_SOURCE_REV')
source = (subprocess.check_output(['git', 'show', f'{revision}:{path}'], cwd=root, text=True)
          if revision else (root / path).read_text())


def method(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise ValueError(signature)


methods = [method(signature) for signature in (
    'void ActivityManager::loop()', 'void ActivityManager::goToSleep(',
    'void ActivityManager::replaceActivity(', 'void ActivityManager::exitActivity(',
    'void ActivityManager::pushActivity(', 'void ActivityManager::popActivity(',
    'bool ActivityManager::requiresExclusiveStorageLoop()')]
if 'void ActivityManager::processPendingActivities()' in source:
    methods.append(method('void ActivityManager::processPendingActivities()'))

program = r'''
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
int lockDepth = 0;
struct Metrics {
  int polls=0, gestures=0, shortcuts=0, exits=0, entries=0, notifications=0, preparations=0, allocations=0;
  bool failAllocation=false;
  std::string preparedReader;
} metrics;
struct RenderLock {
  bool held=true;
  RenderLock(){assert(lockDepth==0);++lockDepth;}
  void unlock(){if(held){--lockDepth;held=false;}}
  ~RenderLock(){unlock();}
};
enum class HomeButtonAction {Ignore,QuickActions};
namespace readershortcuts {enum class Action{QuickActions};}
namespace bleturner {
  bool ready=true;int polls=0;
  bool beforeScreenChange(int,bool){++polls;return ready;}
}
namespace HeaderBackTapTarget {void clear(){}bool contains(int,int){return false;}}
namespace haptic_feedback {void touchAction(){}}
struct Input {
  bool suppressed=false,home=false;int queries=0;
  HomeButtonAction action=HomeButtonAction::Ignore;
  bool consumeSuppressedRelease(){++queries;bool result=suppressed;suppressed=false;return result;}
  HomeButtonAction homeButtonAction(){++queries;return action;}
  bool wasHomeGesture(){++queries;return home;}
  bool hasTouch(){++queries;return false;}
  bool wasScreenTapped(int&,int&){++queries;return false;}
  bool wasLightPanelGesture(){++queries;return false;}
  void resetHomeButtonInput(){home=false;}
};
struct ActivityResult {bool isCancelled=false;};
struct ReaderActivity;
struct Activity {
  std::string name="Popup";bool exclusive=false,home=false;
  ActivityResult result;
  std::function<void(const ActivityResult&)>resultHandler;
  virtual~Activity()=default;
  virtual ReaderActivity*bookReader(){return nullptr;}
  virtual bool isReaderActivity()const{return false;}
  bool isHomeActivity()const{return home;}
  bool requiresExclusiveStorageLoop()const{return exclusive;}
  bool handleHomeGesture(){return false;}
  virtual void loop(){++metrics.polls;}
  virtual void onExit(){assert(lockDepth==1);++metrics.exits;}
  virtual void onEnter(){assert(lockDepth==0);++metrics.entries;}
};
struct ReaderActivity:Activity {
  ReaderActivity(){name="Reader";}
  struct{int pauses=0;void pause(){++pauses;}}readingStats;
  ReaderActivity*bookReader()override{return this;}
  bool isReaderActivity()const override{return true;}
  void finishReaderGestures(){}
  bool handleReaderGestureInput(){++metrics.gestures;return false;}
  void handleReaderShortcut(readershortcuts::Action){++metrics.shortcuts;}
};
struct FrontlightPanelActivity:Activity{FrontlightPanelActivity(int&,Input&){} };
struct SleepActivity:Activity {
  bool fromTimeout;
  SleepActivity(int&,Input&,bool timeout):fromTimeout(timeout){name="Sleep";}
  void prepareReaderPageBackground(ReaderActivity*reader){
    assert(lockDepth==1&&metrics.exits==0);
    ++metrics.preparations;metrics.preparedReader=reader?reader->name:"";
  }
};
template<class T,class...Args>std::unique_ptr<T>makeUniqueNoThrow(Args&&...args){
  ++metrics.allocations;
  return metrics.failAllocation?nullptr:std::make_unique<T>(std::forward<Args>(args)...);
}
struct ActivityManager {
  enum class PendingAction{None,Push,Pop,Replace,ReplaceForSleep};
  int renderer=0,renderTaskHandle=1;
  Input mappedInput;bool inputLocked=false;
  std::unique_ptr<Activity>currentActivity,pendingActivity;
  std::vector<std::unique_ptr<Activity>>stackActivities;
  PendingAction pendingAction=PendingAction::None;
  Activity*pendingHomeReaderTarget=nullptr;
  uint32_t screenVisit=0;
  std::atomic<bool>requestedUpdate{false};
  bool isInputLocked()const{return inputLocked;}
  bool requiresExclusiveStorageLoop()const;
  void notifyRenderTask(){++metrics.notifications;}
  void requestUpdate(){requestedUpdate=true;}
  bool continueHomeReaderUnwind(){return false;}
  void goHome(){auto home=std::make_unique<Activity>();home->home=true;home->name="Home";replaceActivity(std::move(home));}
  void loop();void processPendingActivities();void goToSleep(bool fromTimeout=false);
  void replaceActivity(std::unique_ptr<Activity>&&);void exitActivity(const RenderLock&);
  void pushActivity(std::unique_ptr<Activity>&&);void popActivity();
};
''' + '\n'.join(methods) + r'''
int main(){
  for(bool locked:{false,true})for(bool suppressed:{false,true}){
    metrics={};bleturner::ready=false;bleturner::polls=0;
    ActivityManager manager;manager.currentActivity=std::make_unique<ReaderActivity>();
    manager.inputLocked=locked;manager.mappedInput.suppressed=suppressed;
    manager.mappedInput.action=HomeButtonAction::QuickActions;
    manager.goToSleep(true);
    if(metrics.gestures||metrics.shortcuts||metrics.polls||manager.mappedInput.queries){
      std::fputs("Sleep dispatched stale reader input\n",stderr);return 2;
    }
    assert(metrics.exits==1&&metrics.entries==1&&manager.currentActivity->name=="Sleep");
    assert(manager.pendingAction==ActivityManager::PendingAction::None&&bleturner::polls==0);
    assert(static_cast<SleepActivity*>(manager.currentActivity.get())->fromTimeout);
  }
  metrics={};
  {
    ActivityManager manager;auto reader=std::make_unique<ReaderActivity>();reader->name="Retained Reader";
    manager.stackActivities.push_back(std::move(reader));manager.currentActivity=std::make_unique<Activity>();
    manager.goToSleep();
    assert(manager.stackActivities.empty()&&metrics.exits==2&&metrics.entries==1);
#if CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY
    assert(metrics.preparations==1&&metrics.preparedReader=="Retained Reader");
#else
    assert(metrics.preparations==0);
#endif
  }
  metrics={};
  {
    ActivityManager manager;manager.currentActivity=std::make_unique<Activity>();manager.currentActivity->exclusive=true;
    manager.goToSleep();
    assert(manager.currentActivity->exclusive&&metrics.exits==0&&metrics.allocations==0);
    assert(manager.pendingAction==ActivityManager::PendingAction::None);
    manager.pushActivity(std::make_unique<Activity>());manager.loop();
    assert(metrics.polls==1&&metrics.exits==0&&metrics.entries==0&&manager.currentActivity->exclusive);
  }
  metrics={};metrics.failAllocation=true;
  {
    ActivityManager manager;manager.currentActivity=std::make_unique<ReaderActivity>();manager.goToSleep();
    assert(manager.currentActivity->isReaderActivity()&&metrics.entries==0&&metrics.exits==0);
    assert(manager.pendingAction==ActivityManager::PendingAction::None);
  }
  metrics={};bleturner::ready=true;
  {
    ActivityManager manager;manager.currentActivity=std::make_unique<ReaderActivity>();
    manager.pushActivity(std::make_unique<Activity>());manager.loop();
    assert(manager.stackActivities.size()==1&&metrics.polls==1&&metrics.gestures==1&&metrics.entries==1);
    manager.popActivity();manager.loop();
    assert(manager.stackActivities.empty()&&manager.currentActivity->isReaderActivity());
    assert(metrics.exits==1&&metrics.notifications==1);
  }
  metrics={};
  {
    ActivityManager manager;manager.goToSleep();
    assert(manager.currentActivity->name=="Sleep"&&metrics.entries==1&&metrics.exits==0);
  }
  assert(lockDepth==0);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    for overlay in (0, 1):
        subprocess.run(['c++', '-std=c++20', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        *(['-DCROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY=1'] if overlay else []),
                        str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
print('Sleep skips stale input, preserves reader exit and overlay, and respects exclusive USB storage')
