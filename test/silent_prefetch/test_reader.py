"""Exercise the production prefetch scheduler with controllable reader state."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
def method(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
methods = '\n'.join(method(s) for s in ['bool samePrefetchSpec(',
    'void EpubReaderActivity::abandonNextChapterPrefetch()', 'void EpubReaderActivity::nextChapterPrefetchTick()',
    'void EpubReaderActivity::cancelPrefetchOnInput()'])
# The draft must remain unreachable until publication and setup fixes are reviewed.
assert 'nextChapterPrefetchTick();' not in method('void EpubReaderActivity::backgroundBuildTick()')
shortcut_source = (root / 'src/activities/reader/EpubReaderShortcuts.cpp').read_text()
gesture_start = shortcut_source.index('void EpubReaderActivity::onReaderGestureStarted()')
gesture_end = shortcut_source.index('\n}', gesture_start) + 2
methods += '\n' + shortcut_source[gesture_start:gesture_end]
code = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include "Epub/ReaderRenderSpec.h"
uint32_t tick=10000,nextPrefetchDeadlineMs=0;bool button=false,touch=false,released=false,overlay=false;
unsigned long millis(){return tick;}bool buttonContactClosed(){return button;}bool yieldNextPrefetch(){return button;}
template<class T,class... A>std::unique_ptr<T> makeUniqueNoThrow(A&&...a){return std::make_unique<T>(std::forward<A>(a)...);}
namespace FsHelpers {bool hasEpubExtension(const std::string& s){return s.ends_with(".epub");}}
struct Renderer {int loans=0;bool frame=true,loanOnStart=false;bool hasFrameBuffer(){return frame;}uint32_t frameBufferLoanCount(){return loans;}};
struct Epub {bool protectedContent=false;std::string path="book.epub";bool hasProtectedContent(){return protectedContent;}
 const std::string& getPath(){return path;}int getSpineItemsCount(){return 3;}};
struct Section {
 static bool cached,startOk;static size_t size;static int steps,starts,aborts;bool building=false,complete=false,partial=false;
 int currentPage=8,pageCount=10,remaining=0;Renderer* renderer=nullptr;
 Section()=default;Section(std::shared_ptr<Epub>,int,Renderer& r):renderer(&r){}
 bool isBuilding(){return building;}bool isPartial(){return partial;}bool isBuildComplete(){return complete;}
 bool getHtmlCacheSize(size_t& n){n=size;return cached;}
 bool startBuild(const ReaderRenderSpec&,std::nullptr_t,uint16_t p,uint16_t a){assert(p==32&&a==64);++starts;
  if(renderer->loanOnStart)++renderer->loans;building=startOk;remaining=steps;return startOk;}
 bool buildSomeMore(int n,bool(*)()){assert(n==1&&building);if(--remaining==0){building=false;complete=true;}return true;}
 void abandonBuild(){assert(building);++aborts;building=false;}
};
bool Section::cached=true;bool Section::startOk=true;size_t Section::size=100;int Section::steps=2,Section::starts=0,Section::aborts=0;
struct Settings {struct {bool enabled=false;}ble;ReaderRenderSpec spec;
 ReaderRenderSpec readerRenderSpec(int w,int h){auto s=spec;s.viewportWidth=w;s.viewportHeight=h;return s;}} SETTINGS;
struct Esp {size_t heap=200000,block=100000;size_t getFreeHeap(){return heap;}size_t getMaxAllocHeap(){return block;}} ESP;
enum class HomeButtonAction {Ignore,Home};
struct MappedInputManager {enum class Button{Back,Confirm,Left,Right,Up,Down,Power};};
struct Input {bool touchContactsDown(){return touch;}bool wasScreenTouchReleased(){return released;}bool hasRemoteClick(){return false;}
 HomeButtonAction homeButtonAction(){return HomeButtonAction::Ignore;}bool wasPressed(MappedInputManager::Button){return false;}
 bool wasReleased(MappedInputManager::Button){return false;}};
struct RenderLock {};
struct EpubReaderActivity {
 std::shared_ptr<Epub> epub=std::make_shared<Epub>();std::unique_ptr<Section> section=std::make_unique<Section>();
 std::unique_ptr<Section> nextChapterPrefetch;std::atomic<bool> nextPrefetchActive{false};
 int nextPrefetchSpine=-1,skippedPrefetchSpine=-1,currentSpineIndex=0,pendingManualTurn=0;
 bool automaticPageTurnActive=false,pendingManualTurnTouch=false;
 ReaderRenderSpec nextPrefetchSpec,skippedPrefetchSpec;std::optional<int> pendingSavedView;
 unsigned long lastPrefetchAttemptMs=0,lastRenderCompleteMs=9000;int buildViewportWidth=480,buildViewportHeight=800,updates=0;
 std::atomic<bool> pageBufferStale{false};Renderer renderer;Input mappedInput;
 bool anyButtonPressed(){return button;}bool inputOverPage(){return overlay;}void requestUpdate(){++updates;}
 void abandonNextChapterPrefetch();void nextChapterPrefetchTick();void cancelPrefetchOnInput();
 void onReaderGestureStarted();
};
''' + methods + r'''
void reset(){tick=10000;button=touch=released=overlay=false;Section::cached=true;Section::startOk=true;Section::size=100;
 Section::steps=2;Section::starts=Section::aborts=0;SETTINGS=Settings{};ESP=Esp{};}
int main(){
 reset();EpubReaderActivity normal;normal.nextChapterPrefetchTick();assert(normal.nextPrefetchActive && Section::starts==1);
 normal.nextChapterPrefetchTick();assert(!normal.nextPrefetchActive && normal.skippedPrefetchSpine==1);
 normal.nextChapterPrefetchTick();assert(Section::starts==1);
 reset();Section::cached=false;EpubReaderActivity uncached;uncached.nextChapterPrefetchTick();assert(Section::starts==0);
 reset();Section::size=8193;EpubReaderActivity large;large.nextChapterPrefetchTick();assert(Section::starts==0);
 reset();EpubReaderActivity prot;prot.epub->protectedContent=true;prot.nextChapterPrefetchTick();assert(Section::starts==0);
 reset();EpubReaderActivity txt;txt.epub->path="book.txt";txt.nextChapterPrefetchTick();assert(Section::starts==0);
 reset();EpubReaderActivity radio;SETTINGS.ble.enabled=true;radio.nextChapterPrefetchTick();assert(Section::starts==0);
 reset();EpubReaderActivity memory;ESP.heap=1000;memory.nextChapterPrefetchTick();assert(Section::starts==0);
 reset();EpubReaderActivity input;input.nextChapterPrefetchTick();touch=true;input.cancelPrefetchOnInput();
 assert(!input.nextPrefetchActive && Section::aborts==1);
 reset();EpubReaderActivity gesture;gesture.nextChapterPrefetchTick();gesture.onReaderGestureStarted();
 assert(!gesture.nextPrefetchActive && Section::aborts==1);
 reset();EpubReaderActivity reflow;reflow.nextChapterPrefetchTick();SETTINGS.spec.fontId=3;tick+=6000;
 reflow.nextChapterPrefetchTick();assert(Section::aborts==1 && Section::starts==2);
 reset();EpubReaderActivity loan;loan.renderer.loanOnStart=true;loan.nextChapterPrefetchTick();
 assert(loan.pageBufferStale && loan.nextPrefetchActive && loan.updates==1);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp, exe = Path(tmp) / 'test.cpp', Path(tmp) / 'test'
    cpp.write_text(code)
    subprocess.run(['c++', '-std=c++20', '-I', str(root / 'lib/Epub'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Prefetch reader eligibility, lifecycle, input, and memory checks passed')
