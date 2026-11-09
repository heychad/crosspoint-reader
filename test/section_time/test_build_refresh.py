"""Exercise the real background-build completion refresh decision."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
source=(root/'src/activities/reader/EpubReaderActivity.cpp').read_text()
start=source.index('void EpubReaderActivity::backgroundBuildTick()')
end=source.index('\nvoid EpubReaderActivity::openDictionaryWordSelect()',start)
body=source[start:end]
code=r'''
#include <cassert>
#include <cstdint>
#include <memory>
#define LOG_ERR(...) ((void)0)
constexpr int BACKGROUND_BUILD_PAGES_PER_TICK=2;
enum class ReaderBarItem {TimeLeftSection};
struct Bar {bool enabled=false;bool contains(ReaderBarItem)const{return enabled;}};
struct Settings {bool readerBarsEnabled=false;Bar readerTopBar,readerBottomBar;} SETTINGS;
struct RenderLock {enum class Mode{Try};explicit RenderLock(Mode){}bool ownsLock()const{return true;}};
bool buttonContactClosed(){return false;}
struct Renderer {uint32_t loans=0;uint32_t frameBufferLoanCount(){return loans;}};
struct Section {bool ok=true,complete=true;int currentPage=1,pageCount=10;
 bool buildSomeMore(int,bool(*)()){return ok;}bool isBuildComplete(){return complete;}};
struct Stats {uint32_t remaining=100;uint32_t sectionTimeLeft(int current,int total){assert(current==2&&total==10);return remaining;}};
struct EpubReaderActivity {
 enum class Overlay {None,Text};Overlay overlay=Overlay::None;
 std::unique_ptr<Section> section=std::make_unique<Section>();Renderer renderer;Stats readingStats;
 bool pageBufferStale=false,reposition=false;int pendingManualTurn=0,backgroundBuildFailedSpine=-1,currentSpineIndex=5,nextPageNumber=-1,updates=0;
 bool backgroundBuildWanted(){return true;}bool anyButtonPressed(){return false;}bool buildTickHeapGate(){return true;}
 void nextChapterPrefetchTick(){} void abandonNextChapterPrefetch(){}
 bool applyDeferredReposition(){return reposition;}void requestUpdate(){++updates;}
 void backgroundBuildTick();
};
''' + body + r'''
int main(){
 EpubReaderActivity hidden;hidden.backgroundBuildTick();assert(hidden.updates==0);
 SETTINGS.readerBarsEnabled=true;SETTINGS.readerTopBar.enabled=true;
 EpubReaderActivity ready;ready.backgroundBuildTick();assert(ready.updates==1);
 EpubReaderActivity noPace;noPace.readingStats.remaining=0;noPace.backgroundBuildTick();assert(noPace.updates==0);
 EpubReaderActivity ongoing;ongoing.section->complete=false;ongoing.backgroundBuildTick();assert(ongoing.updates==0);
 EpubReaderActivity reposition;reposition.reposition=true;reposition.backgroundBuildTick();assert(reposition.updates==1);
 EpubReaderActivity failed;failed.section->ok=false;failed.backgroundBuildTick();assert(!failed.section&&failed.nextPageNumber==1&&failed.updates==1);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp);(tmp/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20',str(tmp/'test.cpp'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('Background build refreshes available section estimates')
