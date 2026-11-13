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

method = (extract('hasChapterPrefetchRoom','bool') + '\n' + extract('armChapterPrefetch')
          + '\n' + extract('prefetchNextChapter'))
header = (root / 'src/activities/reader/EpubReaderActivity.h').read_text()
work_limit = re.search(r'static constexpr uint32_t PREFETCH_WORK_LIMIT_MS = \d+;',header).group()
code = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
struct ReaderRenderSpec {};
struct Settings { ReaderRenderSpec readerRenderSpec(int,int){return {};} } SETTINGS;
struct HalMemory {
 struct HeapStats {uint32_t freeBytes,largestBlockBytes;};
 static inline HeapStats normal{256*1024,64*1024},internal{64*1024,32*1024};
 static HeapStats getDefaultHeap(){return normal;} static HeapStats getInternalHeap(){return internal;}
};
namespace bleturner { bool active=false; bool holdsHeap(){return active;} }
struct Gfx { uint32_t loans=0; uint32_t frameBufferLoanCount()const{return loans;} };
struct RenderLock { enum class Mode{Try}; RenderLock(Mode){} bool ownsLock()const{return true;} };
struct Manager {int requests=0; void requestBackgroundRender(){++requests;}} activityManager;
unsigned long nowMs=1000; unsigned long millis(){return nowMs;}
int delays=0; void delay(unsigned long ms){nowMs+=ms;++delays;}
bool rawPressed=false; bool buttonContactClosed(){return rawPressed;}
struct Input {struct Touch {int reportedCount=0;}; Touch touch; Touch getTouchSnapshot(){return touch;}};
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
 Input mappedInput; int pendingManualTurn=0; unsigned long lastRenderCompleteMs=1; bool anyButtonPressed(){return false;}
 bool pageBufferStale=false; int updates=0; void requestUpdate(){++updates;}
 ReaderRenderSpec effectiveRenderSpec(int w,int h,int){return SETTINGS.readerRenderSpec(w,h);}
 void armChapterPrefetch();
 bool hasChapterPrefetchRoom(bool)const;
 void prefetchNextChapter(uint16_t,uint16_t);
};
''' + method + r'''
int main(){
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
