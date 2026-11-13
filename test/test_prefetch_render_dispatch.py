"""Exercise production render-task dispatch without running a physical display."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/ActivityManager.cpp').read_text()
start = source.index('void ActivityManager::renderTaskLoop()')
opening = source.index('{', start)
depth = 0
for index in range(opening, len(source)):
    depth += (source[index] == '{') - (source[index] == '}')
    if not depth:
        method = source[start:index + 1]
        break
else:
    raise RuntimeError('renderTaskLoop not found')
code = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <utility>
#define CROSSPOINT_EXPERIMENTAL_CHAPTER_PREFETCH 1
#define pdTRUE 1
#define portMAX_DELAY 1
#define eIncrement 1
#define taskENTER_CRITICAL(x) ((void)0)
#define taskEXIT_CRITICAL(x) ((void)0)
int activityManagerSpinlock;
int notifications=0, wakes=0;
void ulTaskNotifyTake(int,int){if(wakes++)throw 1;}
using TaskHandle_t=void*;
void xTaskNotify(TaskHandle_t,int,int){++notifications;}
struct RenderLock {};
struct Settings {int screenInverted=0;} SETTINGS;
struct Display {void setInverted(bool){}} display;
namespace HalPowerManager {struct Lock {};}
struct Activity {int normal=0, background=0;void render(RenderLock&&){++normal;}void renderBackground(RenderLock&&){++background;}};
struct ActivityManager {
 std::atomic<uint32_t> normalRenderSequence{0},paintedVisit{0};
 std::atomic<bool> backgroundRenderPending{false},requestedUpdate{false},renderInFlight{false};
 uint32_t backgroundRenderVisit=1,screenVisit=1; bool locked=false,lockedRenderPending=false;
 Activity* currentActivity=nullptr; TaskHandle_t waitingTaskHandle=0;
 bool isInputLocked()const{return locked;}
 void renderTaskLoop();
};
''' + method + r'''
void once(ActivityManager& manager){wakes=0;try{manager.renderTaskLoop();}catch(int){}}
int main(){
 Activity a; ActivityManager m; m.currentActivity=&a; m.backgroundRenderPending=true; m.waitingTaskHandle=&a;
 notifications=0; once(m); assert(a.background==1 && a.normal==0 && m.waitingTaskHandle==&a && notifications==0);
 m.normalRenderSequence=1; m.backgroundRenderPending=true; once(m);
 assert(a.normal==1 && a.background==1 && m.waitingTaskHandle==0 && notifications==1 && m.paintedVisit==1);
 // A pending ordinary redraw wins even before its notification is posted.
 m.normalRenderSequence=0; m.requestedUpdate=true; m.backgroundRenderPending=true; once(m);
 assert(a.normal==2 && a.background==1);
 m.requestedUpdate=false; m.backgroundRenderVisit=2; m.backgroundRenderPending=true; once(m);
 assert(a.normal==2 && a.background==1);
 m.backgroundRenderVisit=1; m.locked=true; m.backgroundRenderPending=true; once(m);
 assert(a.normal==2 && a.background==1 && !m.lockedRenderPending);
 m.locked=false; m.backgroundRenderPending=false; m.normalRenderSequence=1; once(m);
 assert(a.normal==3 && a.background==1);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary)
    (path / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX','c++'), '-std=c++20',
                    *shlex.split(os.environ.get('CXXFLAGS','')),
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Background and normal render dispatch checks passed')
