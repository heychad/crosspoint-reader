"""Exercise the experimental reader prefetch scheduler with deterministic stubs."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
def extract(name):
    start = source.index('void EpubReaderActivity::' + name + '(')
    opening = source.index('{', start)
    depth = 0
    for index in range(opening, len(source)):
        depth += (source[index] == '{') - (source[index] == '}')
        if depth == 0:
            return source[start:index + 1]
    raise RuntimeError(name + ' not found')

method = extract('armChapterPrefetch') + '\n' + extract('prefetchNextChapter')
code = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
struct ReaderRenderSpec {};
struct Settings { ReaderRenderSpec readerRenderSpec(int,int){return {};} } SETTINGS;
struct Heap { int free=64000, block=32000; int getFreeHeap()const{return free;} int getMaxAllocHeap()const{return block;} } ESP;
namespace bleturner { bool active=false; bool holdsHeap(){return active;} }
struct Gfx { uint32_t loans=0; uint32_t frameBufferLoanCount()const{return loans;} };
struct RenderLock { enum class Mode{Try}; RenderLock(Mode){} bool ownsLock()const{return true;} };
struct Manager {int requests=0; void requestBackgroundRender(){++requests;}} activityManager;
unsigned long nowMs=1000; unsigned long millis(){return nowMs;}
bool rawPressed=false; bool buttonContactClosed(){return rawPressed;}
struct Input {struct Touch {int reportedCount=0;}; Touch touch; Touch getTouchSnapshot(){return touch;}};
struct Book { int getSpineItemsCount()const{return 3;} };
struct Section {
 static inline int starts=0, steps=0, destroyed=0; static inline bool cached=false, nextPartial=false, hadPartialAtStart=false, fail=false, cancelOnStep=false;
 static inline void* inputOnLoad=nullptr;
 bool active=false, partial=false; int currentPage=8; int pageCount=10;
 Section()=default; Section(std::shared_ptr<Book>,int,Gfx&){}
 ~Section(){++destroyed;}
 bool isBuilding()const{return active;} bool isPartial()const{return partial;}
 bool loadSectionFile(ReaderRenderSpec){partial=nextPartial;if(inputOnLoad)cancelActivity(inputOnLoad);return cached;}
 bool startBuild(ReaderRenderSpec,void*,bool(*cancel)(void*),void* ctx){++starts;hadPartialAtStart=partial;active=true;return !cancel(ctx);}
 bool buildSomeMore(int, bool(*)(), bool(*cancel)(void*),void* ctx){++steps; if(cancelOnStep)*static_cast<std::atomic<bool>*>(ctx)=true; if(cancel(ctx)) return true; active=false;return !fail;}
};
template<typename T,typename... A> std::unique_ptr<T> makeUniqueNoThrow(A&&... a){return std::make_unique<T>(std::forward<A>(a)...);}
struct EpubReaderActivity {
 enum class Overlay{None,Other};
 std::shared_ptr<Book> epub=std::make_shared<Book>(); std::unique_ptr<Section> section=std::make_unique<Section>(); Gfx renderer;
 std::atomic<uint32_t> prefetchInputSequence{0}; uint32_t prefetchArmedSequence=0;
 int prefetchArmedSpine=0,prefetchAttemptedSpine=-1,currentSpineIndex=0; Overlay overlay=Overlay::None;
 Input mappedInput; int pendingManualTurn=0; unsigned long lastRenderCompleteMs=1; bool anyButtonPressed(){return false;}
 bool pageBufferStale=false; int updates=0; void requestUpdate(){++updates;}
 ReaderRenderSpec effectiveRenderSpec(int w,int h,int){return SETTINGS.readerRenderSpec(w,h);}
 void armChapterPrefetch();
 void prefetchNextChapter(uint16_t,uint16_t);
};
''' + method + r'''
int main(){
 EpubReaderActivity a;
 a.section->currentPage=2; a.prefetchNextChapter(400,600); assert(Section::starts==0);
 a.prefetchArmedSpine=0; a.section->currentPage=8; bleturner::active=true; a.prefetchNextChapter(400,600); assert(Section::starts==0);
 a.prefetchArmedSpine=0; bleturner::active=false; ESP.free=100; a.prefetchNextChapter(400,600); assert(Section::starts==0);
 a.prefetchArmedSpine=0; ESP.free=64000; a.prefetchInputSequence=1;
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
}
'''
# The callback context in the production method is the activity; the stub's
# atomic is not its first field, so cancel through the activity's actual field.
code = code.replace('*static_cast<std::atomic<bool>*>(ctx)=true', 'cancelActivity(ctx)')
code = code.replace('struct Section {', 'void cancelActivity(void*);\nstruct Section {')
marker = 'void EpubReaderActivity::prefetchNextChapter('
code = code.replace(marker, 'void cancelActivity(void* p){++static_cast<EpubReaderActivity*>(p)->prefetchInputSequence;}\n' + marker, 1)
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Chapter prefetch scheduler checks passed')
