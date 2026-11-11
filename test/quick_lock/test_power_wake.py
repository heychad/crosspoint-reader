"""Exercise the production wake decision, release guard, and awake dispatch."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
header = (root / "src/CrossPointSettings.h").read_text()
start = header.index("  uint16_t getPowerButtonDuration() const")
settings = header[start:header.index("  int getReaderFontId()", start)]
source = (root / "src/main.cpp").read_text()
start = source.index("      if (!wakeHoldVerified &&")
wake = source[start:source.index("      wakePowerReleasePending = true;", start)]
start = source.index("  if (wakePowerReleasePending &&")
release = source[start:source.index("  const auto shortcutOutcome", start)]
source = (root / "src/activities/ActivityManager.cpp").read_text()
start = source.index("  if (action == Action::Default", source.index("bool ActivityManager::dispatchShortcut"))
dispatch = source[start:source.index("  if (isInputLocked())", start)]
code = r'''
#include <cassert>
#include "personalization/ReaderShortcuts.h"
using Action=readershortcuts::Action;
struct CrossPointSettings {
 enum class SHORT_PWRBTN {IGNORE,SLEEP};
 uint8_t powerTapAction=0;SHORT_PWRBTN shortPwrBtn=SHORT_PWRBTN::IGNORE;
''' + settings + r'''
} SETTINGS;
bool wakePowerReleasePending=false;
struct {bool pressed=false;bool isPressed(int){return pressed;}bool isUsbConnected(){return false;}}gpio;
struct HalGPIO {enum{BTN_POWER};};
struct {int prepared=0;void prepareForDeepSleep(){++prepared;}}Storage;
struct {int slept=0;template<class T>void startDeepSleep(T&){++slept;}}powerManager;
#define FREEINK_DEVICE_PICCO 0
#define LOG_DBG(...) ((void)0)
void boot(bool wakeHoldVerified){
''' + wake + r'''
 wakePowerReleasePending=true;
}
int inputActions=0;
void loop(){
''' + release + r'''
 ++inputActions;
}
int sleeps=0,locks=0;
void requestSleep(){++sleeps;}void requestInputLock(int){++locks;}
bool dispatch(Action action){int trigger=0;
''' + dispatch + r'''
 return false;
}
int main(){
 for(bool legacy:{false,true})for(Action action:{Action::Default,Action::Sleep,Action::SleepOnly,Action::WakeOnly,Action::NextPage}){
  SETTINGS.powerTapAction=static_cast<uint8_t>(action);
  SETTINGS.shortPwrBtn=legacy?CrossPointSettings::SHORT_PWRBTN::SLEEP:CrossPointSettings::SHORT_PWRBTN::IGNORE;
  const bool shortWake=action==Action::Sleep||action==Action::WakeOnly||(action==Action::Default&&legacy);
  assert(SETTINGS.getPowerButtonDuration()==(shortWake?10:400));
  for(bool held:{false,true}){
   Storage.prepared=0;powerManager.slept=0;boot(held);
   assert(powerManager.slept==(!held&&!shortWake?1:0));
   assert(Storage.prepared==powerManager.slept);
  }
 }
 sleeps=locks=0;
 assert(dispatch(Action::WakeOnly)&&sleeps==0&&locks==0);
 assert(dispatch(Action::SleepOnly)&&sleeps==1);
 assert(dispatch(Action::Sleep)&&sleeps==2);
 assert(!dispatch(Action::Default)&&!dispatch(Action::Disabled));
 wakePowerReleasePending=true;gpio.pressed=true;inputActions=0;loop();
 gpio.pressed=false;loop();assert(!wakePowerReleasePending&&inputActions==1);
 loop();assert(inputActions==2);
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / "test.cpp").write_text(code)
    subprocess.run(["c++", "-std=c++20", "-I", str(root / "src"),
                    str(directory / "test.cpp"), "-o", str(directory / "test")], check=True)
    subprocess.run([str(directory / "test")], check=True)
print("Power wake policy, release guard, and awake dispatch passed")
