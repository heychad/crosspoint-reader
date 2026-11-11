"""Exercise the production reader-to-statistics transition and save result."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/activities/reader/ReaderGestureInput.cpp").read_text()
start = source.index("void ReaderActivity::openReadingStatistics(bool toggleTracking) {")
start = source.index("{", start) + 1
end = source.index("\nbool ReaderActivity::handleReaderShortcut", start)
body = source[start:end].rsplit("}", 1)[0]
code = r'''
#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <utility>
int locks=0;
struct {unsigned char trackReadingStats=1,readingIdleTimeThresholdUnits=30;} SETTINGS;
bool allocationFails=false;
struct RenderLock {template<typename T>explicit RenderLock(T&){++locks;}~RenderLock(){--locks;}};
#define LOG_ERR(...) ((void)0)
struct ActivityResult{};
struct GfxRenderer{};
struct Input{};
struct BookReadingStats{int marker=1;};
struct GlobalReadingStats{static GlobalReadingStats load(){assert(locks>0);return {};}};
namespace readingstats {
BookReadingStats loadBook(const std::string&cache,const std::string&book,bool&tracking){
 assert(locks>0&&cache=="cache"&&book=="book");tracking=false;return{};
}
}
struct ReadingStatsActivity {
 bool toggleRequested=false;void requestTrackingToggle(){toggleRequested=bookContext;}
 bool bookContext=false,saved=true,tracking=true;int progress=-1;void* liveStats=nullptr;
 ReadingStatsActivity(GfxRenderer&,Input&){}
 ReadingStatsActivity(GfxRenderer&,Input&,std::string title,const BookReadingStats&,const GlobalReadingStats&,int p,bool t,bool s,const std::string& cache,const std::string& book,void* live)
  :bookContext(true),saved(s),tracking(t),progress(p),liveStats(live){assert(title=="Title" && cache=="cache" && book=="book");}
};
template<typename T,typename...Args>std::unique_ptr<T>makeUniqueNoThrow(Args&&...args){
 if(allocationFails)return{};return std::make_unique<T>(std::forward<Args>(args)...);
}
struct Reader {
 GfxRenderer renderer;Input mappedInput;std::string bookPath="book";
 std::atomic<bool>pageRendered{true},pendingStatsSaveWarning{false};
 std::function<void(const ActivityResult&)>callback;
 struct Stats {
  bool saved=true, active=true,refreshOk=true;int calls=0, pauses=0,refreshes=0;
  bool refreshTracking(const std::string&c,const std::string&p,bool enabled,uint16_t idle,bool completion){assert(locks>0&&c=="cache"&&p=="book"&&enabled&&idle==300&&!completion);++refreshes;return refreshOk;}
  bool snapshot(BookReadingStats&,GlobalReadingStats&,bool& out){assert(locks>0);++calls;out=saved;return active;}
  void pause(){assert(locks>0);++pauses;}
 } readingStats;
 int updates=0;std::unique_ptr<ReadingStatsActivity>shown;
 std::string getBookCachePath(){assert(locks>0&&pageRendered.load());return "cache";}
 std::string getBookTitle(){assert(locks>0&&pageRendered.load());return "Title";}
 int getProgressBasisPoints(){assert(locks>0&&pageRendered.load());return 3888;}
 void requestUpdate(){++updates;}
 void startActivityForResult(std::unique_ptr<ReadingStatsActivity>screen,std::function<void(const ActivityResult&)>cb){
 assert(locks==0);shown=std::move(screen);callback=cb;
 }
 void open(bool toggleTracking=false){
''' + body + r'''
 }
};
int main(){
 Reader reader;reader.readingStats.saved=false;
 reader.open();assert(reader.shown&&reader.readingStats.calls==1&&!reader.shown->toggleRequested);
 assert(reader.shown->bookContext&&!reader.shown->saved&&reader.shown->tracking&&reader.shown->progress==3888&&reader.shown->liveStats==&reader.readingStats);
 reader.callback({});assert(reader.readingStats.refreshes==0);
 Reader disabled;disabled.readingStats.active=false;
 disabled.open(true);assert(disabled.shown&&!disabled.shown->tracking&&!disabled.shown->liveStats&&disabled.shown->toggleRequested);
 disabled.readingStats.refreshOk=false;disabled.callback({});assert(disabled.readingStats.refreshes==1&&disabled.pendingStatsSaveWarning.load());
 Reader loading;loading.pageRendered=false;
 loading.open(true);assert(loading.shown&&!loading.shown->bookContext&&loading.readingStats.pauses==1&&!loading.shown->toggleRequested);
 loading.callback({});assert(loading.readingStats.refreshes==0);
 Reader failed;allocationFails=true;
 failed.open(true);assert(!failed.shown&&failed.updates==1&&failed.readingStats.calls==1);
 assert(locks==0);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(code)
    subprocess.run(["c++", "-std=c++20", str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("Reader statistics transition, loading fallback and allocation failure checks passed")
