"""Exercise global shortcut ownership, persistence failure, and network entry."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
def function(source, signature):
    start=source.index(signature); opening=source.index('{',start); depth=1; end=opening+1
    while depth:
        depth += (source[end]=='{')-(source[end]=='}'); end+=1
    return source[start:end]
manager=(ROOT/'src/activities/ActivityManager.cpp').read_text()
program=r'''
#include <cassert>
#include <memory>
#include <optional>
#include "personalization/ReaderShortcuts.h"
#define LOG_ERR(...) ((void)0)
using Action=readershortcuts::Action;
using QuickLockTrigger=int;
enum class NetworkMode { JOIN_NETWORK, CONNECT_CALIBRE, CREATE_HOTSPOT };
struct RenderLock {};
struct Settings { unsigned char disablePageTurnGestures=0,frontlightOn=0;bool save=true;int saves=0;bool saveToFile(){++saves;return save;} } SETTINGS;
struct Light {bool present(){return false;}bool isOn(){return false;}void setOn(bool){}} Frontlight;
struct Input {int suppressions=0;bool hasTouch(){return true;}void suppressTouchContact(){++suppressions;}};
struct CrossPointWebServerActivity {
  bool resumed; std::optional<NetworkMode> mode;
  CrossPointWebServerActivity(int,Input&,bool resumed,std::optional<NetworkMode> mode):resumed(resumed),mode(mode){}
};
bool allocationAllowed=true;
template<class T,class... Args> auto makeUniqueNoThrow(Args&&... args){return allocationAllowed?std::make_unique<T>(std::forward<Args>(args)...):std::unique_ptr<T>{};}
struct Reader {int calls=0;bool handleReaderShortcut(Action,QuickLockTrigger){++calls;return true;}Reader* bookReader(){return this;}};
struct ActivityManager {
 int renderer=0,updates=0;Input mappedInput;bool locked=false,foreground=true;
 Reader reader;Reader* currentActivity=&reader;
 std::unique_ptr<CrossPointWebServerActivity> network;
 bool isInputLocked(){return locked;}bool isForegroundReader(){return foreground;}
 void requestInputLock(QuickLockTrigger){} void requestSleep(){} bool handleHomeReaderShortcut(){return true;}
 void goHome(){}void goToFileBrowser(){}void goToLibrary(){}bool handleForcedRefresh(){return true;}
 void requestUpdate(){++updates;}
 void replaceActivity(std::unique_ptr<CrossPointWebServerActivity> next){network=std::move(next);}
 void goToFileTransferMode(NetworkMode);
 bool dispatchShortcut(Action,QuickLockTrigger);
};
''' + function(manager,'void ActivityManager::goToFileTransferMode(') + '\n' + function(manager,'bool ActivityManager::dispatchShortcut(') + r'''
int main(){
 ActivityManager m;
 assert(m.dispatchShortcut(Action::TogglePageGestures,0));
 assert(SETTINGS.disablePageTurnGestures==1 && SETTINGS.saves==1 && m.reader.calls==0 && m.mappedInput.suppressions==1);
 assert(m.dispatchShortcut(Action::TogglePageGestures,0));assert(SETTINGS.disablePageTurnGestures==0);
 SETTINGS.save=false;assert(m.dispatchShortcut(Action::TogglePageGestures,0));assert(SETTINGS.disablePageTurnGestures==0);
 m.locked=true;int saves=SETTINGS.saves;assert(!m.dispatchShortcut(Action::TogglePageGestures,0));assert(SETTINGS.saves==saves);
 assert(!m.dispatchShortcut(Action::JoinNetwork,0) && !m.network);m.locked=false;
 for(bool foreground:{false,true}){
  m.foreground=foreground;
  assert(m.dispatchShortcut(Action::JoinNetwork,0));assert(m.network->mode==NetworkMode::JOIN_NETWORK && !m.network->resumed);
  assert(m.dispatchShortcut(Action::CreateHotspot,0));assert(m.network->mode==NetworkMode::CREATE_HOTSPOT);
  assert(m.dispatchShortcut(Action::CalibreWireless,0));assert(m.network->mode==NetworkMode::CONNECT_CALIBRE);
 }
 auto* previous=m.network.get();allocationAllowed=false;
 assert(m.dispatchShortcut(Action::JoinNetwork,0));assert(m.network.get()==previous && m.reader.calls==0);
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory);(p/'test.cpp').write_text(program)
    subprocess.run(['c++','-std=c++20','-I',str(ROOT/'src'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('Global shortcuts preserve lock ownership, rollback failed saves, and select network modes without recursion')
