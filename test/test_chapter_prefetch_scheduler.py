"""Exercise the experimental reader prefetch scheduler with deterministic stubs."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
def extract(name, return_type='void'):
    start = source.index(return_type + ' EpubReaderActivity::' + name + '(')
    opening = source.index('{', start)
    depth = 0
    for index in range(opening, len(source)):
        depth += (source[index] == '{') - (source[index] == '}')
        if depth == 0:
            return source[start:index + 1]
    raise RuntimeError(name + ' not found')

method = (extract('reclaimBackgroundCachesIfNeeded','bool') + '\n' + extract('buildTickHeapGate','bool') + '\n'
          + extract('hasChapterPrefetchRoom','bool') + '\n' + extract('armChapterPrefetch')
          + '\n' + extract('prefetchNextChapter') + '\n' + extract('prewarmNextPageWhenIdle')
          + '\n' + extract('hasPendingReaderInput','bool'))
loop = extract('loop')
idle_dispatch = loop[loop.index('if (!prevTriggered && !nextTriggered)'):].split('return;', 1)[0]
assert loop.count('prewarmNextPageWhenIdle();') == 1 and 'if (!atEndOfBook) prewarmNextPageWhenIdle();' in idle_dispatch
header = (root / 'src/activities/reader/EpubReaderActivity.h').read_text()
work_limit = re.search(r'static constexpr uint32_t PREFETCH_WORK_LIMIT_MS = \d+;',header).group()
code = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
struct ReaderRenderSpec {};
struct Settings { ReaderRenderSpec readerRenderSpec(int,int){return {};} int getReaderFontId(){return 17;} } SETTINGS;
struct HalMemory {
 struct HeapStats {uint32_t freeBytes,largestBlockBytes;};
 static inline HeapStats normal{256*1024,64*1024},internal{64*1024,32*1024};
 static HeapStats getDefaultHeap(){return normal;} static HeapStats getInternalHeap(){return internal;}
};
namespace bleturner { bool active=false; bool holdsHeap(){return active;} }
struct Esp {
 size_t getFreeHeap(){return HalMemory::normal.freeBytes;}
 size_t getMaxAllocHeap(){return HalMemory::normal.largestBlockBytes;}
} ESP;
struct ImageBlock { static inline int reclaimed=0; static void releaseRenderCache(){++reclaimed;} };
#define LOG_DBG(...) ((void)0)
struct FontCache {
 int scans=0;
 struct Scope {int& scans; void endScanAndPrewarm(){++scans;}};
 Scope createPrewarmScope(){return {scans};}
 bool recover=false; int reclaimed=0;
 void releaseInactiveSdFontCaches(int id){
  assert(id==17);
  ++reclaimed;
  if(recover){HalMemory::normal={256*1024,64*1024};HalMemory::internal={64*1024,32*1024};}
 }
};
struct Gfx {
 uint32_t loans=0; FontCache fonts;
 uint32_t frameBufferLoanCount()const{return loans;}
 bool available=true,hasFonts=true;
 bool hasFrameBuffer()const{return available;}
 FontCache* getFontCacheManager(){return hasFonts ? &fonts : nullptr;}
};
struct RenderLock { enum class Mode{Try}; RenderLock(Mode){} bool ownsLock()const{return true;} };
struct Manager {int requests=0; void requestBackgroundRender(){++requests;}} activityManager;
unsigned long nowMs=1000; unsigned long millis(){return nowMs;}
int delays=0; void delay(unsigned long ms){nowMs+=ms;++delays;}
bool rawPressed=false; bool buttonContactClosed(){return rawPressed;}
struct Input {struct Touch {int reportedCount=0;}; Touch touch; bool touchContactsDown() const {return touch.reportedCount != 0;}};
struct Book { int getSpineItemsCount()const{return 3;} };
struct Section {
 static inline int starts=0, steps=0, destroyed=0; static inline bool cached=false, nextPartial=false, hadPartialAtStart=false, fail=false, cancelOnStep=false;
 static inline int workSteps=1,suspended=0;
 static inline uint32_t elapsedOnStart=0,elapsedOnStep=0;
 static inline bool lowInternalOnStep=false,radioOnStep=false,failStart=false;
 static inline void* inputOnLoad=nullptr;
 bool active=false, partial=false; int currentPage=8; int pageCount=10;int remaining=0;
 Section()=default; Section(std::shared_ptr<Book>,int,Gfx&){}
 ~Section(){++destroyed;if(active)++suspended;}
 int reads=0, renders=0, loadedPage=-1;
 struct Page {
   int& renders;
   void render(Gfx&,int font,int x,int y){assert(font==17 && x==0 && y==0);++renders;}
 };
 std::unique_ptr<Page> loadPage(int page){++reads;loadedPage=page;return std::make_unique<Page>(Page{renders});}
 bool isBuilding()const{return active;} bool isPartial()const{return partial;}
 bool loadSectionFile(ReaderRenderSpec){partial=nextPartial;if(inputOnLoad)cancelActivity(inputOnLoad);return cached;}
 bool startBuild(ReaderRenderSpec,void*,bool(*cancel)(void*),void* ctx){++starts;hadPartialAtStart=partial;nowMs+=elapsedOnStart;remaining=workSteps;active=!failStart;return active&&!cancel(ctx);}
 bool buildSomeMore(int, bool(*)(), bool(*cancel)(void*),void* ctx){++steps;nowMs+=elapsedOnStep;if(lowInternalOnStep)HalMemory::internal.freeBytes=8*1024;if(radioOnStep)bleturner::active=true;if(cancelOnStep)*static_cast<std::atomic<bool>*>(ctx)=true;if(cancel(ctx))return true;if(--remaining>0)return true;active=false;return !fail;}
};
template<typename T,typename... A> std::unique_ptr<T> makeUniqueNoThrow(A&&... a){return std::make_unique<T>(std::forward<A>(a)...);}
struct EpubReaderActivity {
 enum class Overlay{None,Other};
 std::shared_ptr<Book> epub=std::make_shared<Book>(); std::unique_ptr<Section> section=std::make_unique<Section>(); Gfx renderer;
 std::atomic<uint32_t> prefetchInputSequence{0}; uint32_t prefetchArmedSequence=0,prefetchStartedMs=0;
 WORK_LIMIT
 int prefetchArmedSpine=0,prefetchAttemptedSpine=-1,currentSpineIndex=0; Overlay overlay=Overlay::None;
 Input mappedInput; int pendingManualTurn=0; unsigned long lastRenderCompleteMs=1; bool pressed=false; bool anyButtonPressed() const {return pressed;}
 bool hasPendingReaderInput() const;
 int idlePrewarmPage=-1,idlePrewarmSpine=-1;
 static constexpr size_t RENDER_MIN_FREE_HEAP=32*1024;
 void prewarmNextPageWhenIdle();
 bool pageBufferStale=false; int updates=0; void requestUpdate(){++updates;}
 bool backgroundCacheReclaimAttempted=false,buildHeapPaused=false;
 static constexpr size_t BACKGROUND_BUILD_MIN_FREE_HEAP=32*1024, BACKGROUND_BUILD_MIN_MAX_ALLOC=16*1024;
 ReaderRenderSpec effectiveRenderSpec(int w,int h,int){return SETTINGS.readerRenderSpec(w,h);}
 void armChapterPrefetch();
 bool reclaimBackgroundCachesIfNeeded(bool);
 bool buildTickHeapGate();
 bool hasChapterPrefetchRoom(bool)const;
 void prefetchNextChapter(uint16_t,uint16_t);
};
''' + method + r'''
int main(){
 {
  EpubReaderActivity reader;
  auto& section=*reader.section;
  reader.pendingManualTurn=1;
  reader.prewarmNextPageWhenIdle();
  reader.pendingManualTurn=0;
  rawPressed=true;
  reader.prewarmNextPageWhenIdle();
  rawPressed=false;
  reader.pressed=true;
  reader.prewarmNextPageWhenIdle();
  reader.pressed=false;
  reader.mappedInput.touch.reportedCount=1;
  reader.prewarmNextPageWhenIdle();
  reader.mappedInput.touch.reportedCount=0;
  reader.lastRenderCompleteMs=nowMs-400;
  reader.prewarmNextPageWhenIdle();
  reader.lastRenderCompleteMs--;
  assert(section.reads==0);
  reader.prewarmNextPageWhenIdle();
  reader.prewarmNextPageWhenIdle();
  assert(section.reads==1 && section.loadedPage==9 && section.renders==1 && reader.renderer.fonts.scans==1);
  section.currentPage=section.pageCount-1;
  reader.prewarmNextPageWhenIdle();
  section.currentPage=0;
  reader.renderer.hasFonts=false;
  reader.prewarmNextPageWhenIdle();
  assert(section.reads==1);
 }

 {
  EpubReaderActivity low;
  low.prefetchArmedSpine=-1;
  HalMemory::normal={16*1024,8*1024};
  const auto images=ImageBlock::reclaimed;
  const auto requests=activityManager.requests;
  rawPressed=true;
  low.armChapterPrefetch();
  rawPressed=false;
  assert(ImageBlock::reclaimed==images);
  low.armChapterPrefetch();
  low.armChapterPrefetch();
  assert(ImageBlock::reclaimed==images+1 && low.renderer.fonts.reclaimed==1);
  assert(activityManager.requests==requests);
  HalMemory::normal={256*1024,64*1024};
  assert(low.buildTickHeapGate());
  HalMemory::normal={16*1024,8*1024};
  low.renderer.fonts.recover=true;
  assert(low.buildTickHeapGate() && !low.buildHeapPaused);
  assert(low.renderer.fonts.reclaimed==2);
  assert(low.buildTickHeapGate());  // A later healthy observation re-arms the gate.
  HalMemory::internal={16*1024,8*1024};
  low.armChapterPrefetch();
  assert(low.renderer.fonts.reclaimed==3 && activityManager.requests==requests+1);
  activityManager.requests=requests;
 }
 EpubReaderActivity a;
 a.section->currentPage=2; a.prefetchNextChapter(400,600); assert(Section::starts==0);
 a.prefetchArmedSpine=0; a.section->currentPage=8; bleturner::active=true; a.prefetchNextChapter(400,600); assert(Section::starts==0);
 a.prefetchArmedSpine=0; bleturner::active=false; HalMemory::normal.freeBytes=100; a.prefetchNextChapter(400,600); assert(Section::starts==0);
 a.prefetchArmedSpine=0; HalMemory::normal.freeBytes=256*1024; a.prefetchInputSequence=1;
 a.prefetchNextChapter(400,600); assert(Section::starts==0); // input after arming
 a.prefetchArmedSpine=0; a.prefetchArmedSequence=1;
 a.prefetchNextChapter(400,600); assert(Section::starts==1 && Section::steps==1);
 a.prefetchNextChapter(400,600); assert(Section::starts==1);
 a.currentSpineIndex=1; a.prefetchArmedSpine=1; Section::cancelOnStep=true;
 a.prefetchNextChapter(400,600); assert(Section::starts==2 && Section::steps==2 && a.prefetchInputSequence==2);
 a.currentSpineIndex=2; a.prefetchArmedSpine=2; a.prefetchArmedSequence=2; a.prefetchNextChapter(400,600); assert(Section::starts==2);
 a.currentSpineIndex=0; a.prefetchAttemptedSpine=-1; Section::cached=true; Section::nextPartial=false;
 a.prefetchArmedSpine=0;
 a.prefetchNextChapter(400,600); assert(Section::starts==2);
 Section::cached=false; a.prefetchAttemptedSpine=-1; a.overlay=EpubReaderActivity::Overlay::Other;
 a.prefetchArmedSpine=0;
 a.prefetchNextChapter(400,600); assert(Section::starts==2);
 EpubReaderActivity b; b.prefetchArmedSpine=-1; b.lastRenderCompleteMs=900;
 b.armChapterPrefetch(); assert(activityManager.requests==0);
 b.lastRenderCompleteMs=1; b.mappedInput.touch.reportedCount=1; b.armChapterPrefetch(); assert(activityManager.requests==0);
 b.mappedInput.touch.reportedCount=0; b.armChapterPrefetch(); assert(activityManager.requests==1 && b.prefetchArmedSpine==0);
 b.armChapterPrefetch(); assert(activityManager.requests==1);
 ++b.prefetchInputSequence; b.prefetchNextChapter(400,600); assert(Section::starts==2);
 b.armChapterPrefetch(); assert(activityManager.requests==2 && b.prefetchArmedSequence==1);
 Section::cached=true; Section::nextPartial=true; Section::cancelOnStep=true;
 b.prefetchNextChapter(400,600); assert(Section::starts==3 && Section::hadPartialAtStart);
 EpubReaderActivity c; Section::cached=false; Section::nextPartial=false; Section::inputOnLoad=&c;
 c.prefetchNextChapter(400,600); assert(Section::starts==3 && c.prefetchInputSequence==1);

 // PSRAM/default space cannot admit work with insufficient internal RAM or a fragmented block.
 Section::inputOnLoad=nullptr;Section::cached=false;Section::cancelOnStep=false;
 EpubReaderActivity d;
 const auto idlePosition=d.section->currentPage;const auto idleTotal=d.section->pageCount;
 for(bool fragmented:{false,true}){
  HalMemory::internal={fragmented?64u*1024:31u*1024,fragmented?15u*1024:32u*1024};
  const int starts=Section::starts;d.prefetchArmedSpine=0;d.prefetchNextChapter(400,600);
  assert(Section::starts==starts);
  const int requests=activityManager.requests;d.armChapterPrefetch();assert(activityManager.requests==requests);
 }
 HalMemory::internal={64*1024,32*1024};
 HalMemory::normal={2*1024*1024,47*1024};d.prefetchArmedSpine=0;
 d.prefetchNextChapter(400,600);assert(d.prefetchAttemptedSpine==-1);
 HalMemory::normal={256*1024,64*1024};

 // Normal work yields between pages and leaves the active reader and display unchanged.
 Section::workSteps=4;int before=Section::steps;const int naps=delays;
 d.prefetchArmedSpine=0;d.prefetchNextChapter(400,600);
 assert(Section::steps==before+4 && delays==naps+3);
 assert(d.section->currentPage==idlePosition && d.section->pageCount==idleTotal && d.currentSpineIndex==0);
 assert(!d.pageBufferStale && d.updates==0 && d.renderer.loans==0);
 d.prefetchArmedSpine=0;before=Section::starts;d.prefetchNextChapter(400,600);assert(Section::starts==before);

 // A start that exceeds the idle-work budget does not begin page parsing.
 EpubReaderActivity e;Section::workSteps=100;Section::elapsedOnStart=e.PREFETCH_WORK_LIMIT_MS;
 before=Section::steps;int suspended=Section::suspended;e.prefetchNextChapter(400,600);
 assert(Section::steps==before && Section::suspended==suspended+1);
 Section::elapsedOnStart=0;

 // The same budget stops a long chapter between parse steps, including timer wrap.
 for(bool wrap:{false,true}){
  EpubReaderActivity reader;nowMs=wrap?0xfffffff0u:1000u;Section::elapsedOnStep=reader.PREFETCH_WORK_LIMIT_MS/2;
  before=Section::steps;suspended=Section::suspended;reader.prefetchNextChapter(400,600);
  assert(Section::steps==before+2 && Section::suspended==suspended+1);
  assert(reader.section->currentPage==8 && reader.currentSpineIndex==0);
 }
 Section::elapsedOnStep=0;

 // Memory/radio pressure after admission ends work and destroys the temporary builder.
 for(bool radio:{false,true}){
  EpubReaderActivity reader;Section::lowInternalOnStep=!radio;Section::radioOnStep=radio;
  before=Section::steps;suspended=Section::suspended;reader.prefetchNextChapter(400,600);
  assert(Section::steps==before+1 && Section::suspended==suspended+1);
  HalMemory::internal={64*1024,32*1024};bleturner::active=false;
 }
 Section::lowInternalOnStep=false;Section::radioOnStep=false;Section::workSteps=1;
 EpubReaderActivity f;Section::failStart=true;before=Section::steps;f.prefetchNextChapter(400,600);
 assert(Section::steps==before && !f.pageBufferStale);Section::failStart=false;
}
'''
code = code.replace('\n WORK_LIMIT\n','\n '+work_limit+'\n')
# The callback context in the production method is the activity; the stub's
# atomic is not its first field, so cancel through the activity's actual field.
code = code.replace('*static_cast<std::atomic<bool>*>(ctx)=true', 'cancelActivity(ctx)')
code = code.replace('struct Section {', 'void cancelActivity(void*);\nstruct Section {')
marker = 'void EpubReaderActivity::prefetchNextChapter('
code = code.replace(marker, 'void cancelActivity(void* p){++static_cast<EpubReaderActivity*>(p)->prefetchInputSequence;}\n' + marker, 1)
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary)
    (path / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX','c++'), '-std=c++20',
                    *shlex.split(os.environ.get('CXXFLAGS','')),
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Chapter prefetch scheduler checks passed')
