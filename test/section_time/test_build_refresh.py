"""Exercise the real background-build completion refresh decision."""
from pathlib import Path
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
source=(root/'src/activities/reader/EpubReaderActivity.cpp').read_text()
start=source.index('void EpubReaderActivity::backgroundBuildTick()')
opening=source.index('{',start)
depth=1
end=opening+1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
body=source[start:end]
code=r'''
#include <cassert>
#include <cstdint>
#include <memory>
#define LOG_ERR(...) ((void)0)
constexpr int BACKGROUND_BUILD_PAGES_PER_TICK=2;
enum class ReaderBarItem {TimeLeftSection,TimeLeftChapter};
struct Bar {bool enabled=false,chapter=false;bool contains(ReaderBarItem item)const{return item==ReaderBarItem::TimeLeftSection?enabled:chapter;}};
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
 bool applyDeferredReposition(){return reposition;}void requestUpdate(){++updates;}
 bool tryRenderRecovery(){return false;}
 void backgroundBuildTick();
};
''' + body + r'''
int main(){
 EpubReaderActivity hidden;hidden.backgroundBuildTick();assert(hidden.updates==0);
 SETTINGS.readerBarsEnabled=true;SETTINGS.readerTopBar.enabled=true;
 EpubReaderActivity ready;ready.backgroundBuildTick();assert(ready.updates==1);
 EpubReaderActivity noPace;noPace.readingStats.remaining=0;noPace.backgroundBuildTick();assert(noPace.updates==0);
#if defined(CROSSPOINT_EXPERIMENTAL_CHAPTER_TIME)
 SETTINGS.readerTopBar.enabled=false;SETTINGS.readerTopBar.chapter=true;
 EpubReaderActivity chapter;chapter.readingStats.remaining=0;chapter.backgroundBuildTick();assert(chapter.updates==1);
 SETTINGS.readerTopBar.chapter=false;SETTINGS.readerTopBar.enabled=true;
#endif
 EpubReaderActivity ongoing;ongoing.section->complete=false;ongoing.backgroundBuildTick();assert(ongoing.updates==0);
 EpubReaderActivity reposition;reposition.reposition=true;reposition.backgroundBuildTick();assert(reposition.updates==1);
 EpubReaderActivity failed;failed.section->ok=false;failed.backgroundBuildTick();assert(!failed.section&&failed.nextPageNumber==1&&failed.updates==1);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp);(tmp/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20',*(['-DCROSSPOINT_EXPERIMENTAL_CHAPTER_TIME=1'] if os.getenv('TEST_CHAPTER_TIME') else []),str(tmp/'test.cpp'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('Background build refreshes available section estimates')
