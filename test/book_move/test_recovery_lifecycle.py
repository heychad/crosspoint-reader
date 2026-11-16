"""Exercise production boot routing and pending-writer guards after a failed move."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
main = (ROOT / 'src/main.cpp').read_text()
initialization_start = main.index('  setupDisplayAndFonts(resume != BootResume::Splash);')
initialization_end = main.index('  switch (resume)', initialization_start)
initialization = main[initialization_start:initialization_end]
route_start = main.index('  if (recoveryFirmwareMode)', main.index('// Output polarity is resolved per render'))
route_end = main.index('\n  if (resume == BootResume::Silent)', route_start)
routing = main[route_start:route_end]

web = (ROOT / 'src/network/CrossPointWebServer.cpp').read_text()
start = web.index('void CrossPointWebServer::onWebSocketEvent(')
web_guard = web[start:web.index('  switch (type)', start)] + '  ++writes;\n}\n'

program = r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#define LOG_INF(...) ((void)0)
enum class BootResume{Splash,Silent,SplashlessWake};
bool mainReady=false;unsigned recoveryCalls=0;
namespace bookmove {
bool held=true,canRecover=false;bool pending(){return held;}
int recover(){assert(mainReady);++recoveryCalls;if(canRecover)held=false;return held?1:0;}
}
struct State{std::string openEpubPath="/old.epub";bool lastSleepFromReader=true;int readerActivityLoadCount=0,saves=0;
 void saveToFile(){++saves;}}APP_STATE;
struct SdFirmwareUpdateActivity{template<class Input>SdFirmwareUpdateActivity(int,Input&,bool){}};
struct MappedInputManager{enum class Button{Back};bool isPressed(Button){return false;}}mappedInputManager;
enum class HomeMenuItem{NONE};
struct Manager {
 unsigned transfer=0,reader=0,home=0;
 void replaceActivity(std::unique_ptr<SdFirmwareUpdateActivity>){}void goToCrashReport(){}
 void goToFileTransfer(){++transfer;}void goToReader(const std::string&,bool=false){++reader;}
 void goToJoinNetwork(){}void goToSettings(int){}void goHome(HomeMenuItem=HomeMenuItem::NONE,bool=false){++home;}
}activityManager;
struct SettingsSection{static constexpr int Network=0;};
constexpr int SILENT_REBOOT_TARGET_READER=1,SILENT_REBOOT_TARGET_JOIN_NETWORK=2,SILENT_REBOOT_TARGET_SETTINGS=3;
bool recoveryFirmwareMode=false,rebootedFromPanic=false,needsWakeRefresh=false,allowFastInitialReaderRefresh=false;
int snapshotTarget=0,renderer=0;
void setupDisplayAndFonts(bool){mainReady=true;}
void boot(BootResume resume){
''' + initialization + routing + r'''
}
enum WStype_t{WStype_TEXT,WStype_BIN,WStype_CONNECTED};
struct Ws{unsigned errors=0;void sendTXT(uint8_t,const char*){++errors;}};
struct CrossPointWebServer{Ws* wsServer;unsigned writes=0;void onWebSocketEvent(uint8_t,WStype_t,uint8_t*,size_t);};
''' + web_guard + r'''
int main(){
 boot(BootResume::SplashlessWake);
 assert(recoveryCalls==1&&activityManager.transfer==1&&activityManager.reader==0);
 assert(APP_STATE.openEpubPath=="/old.epub"&&APP_STATE.saves==0&&APP_STATE.readerActivityLoadCount==0);
 bookmove::canRecover=true;mainReady=false;boot(BootResume::SplashlessWake);
 assert(recoveryCalls==2&&!bookmove::held&&activityManager.reader==1);
 Ws ws;CrossPointWebServer web{&ws};bookmove::held=true;
 web.onWebSocketEvent(1,WStype_TEXT,nullptr,0);web.onWebSocketEvent(1,WStype_BIN,nullptr,0);
 assert(web.writes==0&&ws.errors==2);
 bookmove::held=false;web.onWebSocketEvent(1,WStype_TEXT,nullptr,0);assert(web.writes==1);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    (directory / 'test.cpp').write_text(program)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', *shlex.split(os.environ.get('CXXFLAGS', '')),
                    str(directory / 'test.cpp'), '-o', str(directory / 'test')], check=True)
    subprocess.run([str(directory / 'test')], check=True)
print('Boot initializes main ownership before recovery; unresolved routing retains the book path; WebSocket writes stop')
