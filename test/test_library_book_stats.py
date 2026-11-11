"""Exercise the Library statistics launch and index recovery paths."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/library/LibraryListActivity.cpp').read_text()
start = source.index('void LibraryListActivity::openBookStatistics(')
end = source.index('\n#if FREEINK_CAP_TOUCH', start)
functions = source[start:end]
assert 'GlobalReadingStats::loadReadOnly()' in functions
assert 'BookReadingStats::LoadMode::ReadOnly' in functions
menu = source[source.index('void LibraryListActivity::showRecentBookOptions('):start]
assert 'isStoreRow ? 5 : 4' in menu
assert 'case 3:' in menu and 'case 4:' in menu and menu.count('openBookStatistics(path, title)') == 2
assert menu.index('case 1:') < menu.index('promptRemoveRecentBook(path, title)') < menu.index('case 2:')
loop = source[source.index('void LibraryListActivity::loop()'):]
assert loop.index('UiTabListActivity::loop();') < loop.index('if (statisticsOpenFailed)') < loop.index('optionPopup.showMessage')
code = r'''
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#define LOG_ERR(...) ((void)0)
struct BookReadingStats {enum class LoadMode{ReadOnly};bool writable=true;};
struct GlobalReadingStats {bool writable=true;static GlobalReadingStats loadReadOnly(){return {};}};
struct ActivityResult{};
struct HalStorage {enum class PathStatus{Present,Missing,Failed};};
struct StorageType {HalStorage::PathStatus status=HalStorage::PathStatus::Present;HalStorage::PathStatus lookupPath(const char*){return status;}} Storage;
namespace library {bool dirty=false;bool isLibraryIndexDirty(){return dirty;}const char* libraryIndexPath(){return "index";}}
struct Index {bool opened=true,openOk=true;bool isOpen(){return opened;}void close(){opened=false;}bool open(const char*){opened=openOk;return opened;}bool ranksDegraded(){return false;}};
constexpr int STR_LIBRARY_REBUILDING=1;
int tr(int value){return value;}
struct Gui {void drawPopup(int&,int){}} GUI;
namespace FsHelpers {
bool hasReflowableBookExtension(const std::string& p){return p=="book.epub" || p=="book.md";}
bool hasEpubExtension(const std::string& p){return p=="book.epub";}
bool hasXtcExtension(const std::string& p){return p=="book.xtc";}
}
struct Epub {static std::string cachePathForFilePath(const std::string&,const char*){return "epub-cache";}};
struct Xtc {Xtc(const std::string&,const char*){} std::string getCachePath(){return "xtc-cache";}};
namespace readingstats {
bool foundValue=true,knownValue=true,trackingValue=true;int calls=0;
BookReadingStats loadBook(const std::string&,const std::string&,bool& tracking,bool* found,BookReadingStats::LoadMode mode,bool* known){
 assert(mode==BookReadingStats::LoadMode::ReadOnly);++calls;tracking=trackingValue;*found=foundValue;*known=knownValue;return {};}
}
int progressCalls=0;int loadBookProgress(const std::string&){++progressCalls;return 42;}
struct ReadingStatsActivity {
 bool bookWritable;int progress;bool tracking;std::string cache,path;
 ReadingStatsActivity(int&,int&,const std::string&,const BookReadingStats& book,const GlobalReadingStats&,int p,bool t,bool saved,const std::string& c,const std::string& b,void* live,bool closedDateEdit)
 :bookWritable(book.writable),progress(p),tracking(t),cache(c),path(b){assert(saved && live==nullptr && closedDateEdit);}
};
int allocation=0,failAt=0;
template<class T,class... Args>std::unique_ptr<T> makeUniqueNoThrow(Args&&... args){
 ++allocation;if(failAt==allocation)return {};return std::make_unique<T>(std::forward<Args>(args)...);}
struct LibraryListActivity;
bool locked=false;
struct RenderLock {explicit RenderLock(LibraryListActivity&){assert(!locked);locked=true;}void unlock(){assert(locked);locked=false;}~RenderLock(){locked=false;}};
struct LibraryListActivity {
 Index index;int renderer=0,mappedInput=0;bool filterFailed=false,statisticsOpenFailed=false,degraded=false,rebuildOk=true;int filteredCount=3,pinnedTotal=2,closeCount=0,updates=0,releases=0,pushes=0,rebuilds=0,resets=0;
 std::unique_ptr<ReadingStatsActivity> child;std::function<void(const ActivityResult&)> callback;
 void closeRouting(){++closeCount;}void requestUpdate(){++updates;}void swallowHeldReleases(){++releases;}
 bool rebuildIndex(){++rebuilds;return rebuildOk;}void resetAfterRebuild(){++resets;}
 void startActivityForResult(std::unique_ptr<ReadingStatsActivity> activity,std::function<void(const ActivityResult&)> cb){assert(!locked);++pushes;child=std::move(activity);callback=std::move(cb);}
 void openBookStatistics(const std::string&,const std::string&);void restoreIndexAfterStatistics(bool);
};
''' + functions + r'''
int main(){
 LibraryListActivity ok;allocation=0;ok.openBookStatistics("book.epub","Title");
 assert(ok.pushes==1 && !ok.index.isOpen() && ok.child->bookWritable && ok.child->progress==4200 && ok.child->tracking);
 ok.callback({});assert(ok.index.isOpen() && ok.releases==1 && !ok.filterFailed);
 LibraryListActivity missing;Storage.status=HalStorage::PathStatus::Missing;missing.openBookStatistics("book.epub","Title");
 assert(missing.pushes==0 && missing.index.isOpen() && missing.statisticsOpenFailed);Storage.status=HalStorage::PathStatus::Present;
 LibraryListActivity unsupported;allocation=0;unsupported.openBookStatistics("book.pdf","Title");
 assert(unsupported.pushes==0 && unsupported.index.isOpen() && unsupported.statisticsOpenFailed);
 for(int failure : {1,2}) {LibraryListActivity oom;allocation=0;failAt=failure;oom.openBookStatistics("book.epub","Title");
  assert(oom.pushes==0 && oom.index.isOpen() && oom.statisticsOpenFailed);}failAt=0;
 LibraryListActivity noStats;allocation=0;readingstats::foundValue=false;noStats.openBookStatistics("book.epub","Title");
 assert(noStats.pushes==1 && !noStats.child->bookWritable && noStats.child->tracking);readingstats::foundValue=true;
 LibraryListActivity unknown;allocation=0;readingstats::knownValue=false;unknown.openBookStatistics("book.epub","Title");
 assert(unknown.pushes==1 && !unknown.child->bookWritable);readingstats::knownValue=true;
 LibraryListActivity text;allocation=0;int before=progressCalls;text.openBookStatistics("book.md","Title");
 assert(text.pushes==1 && text.child->progress==-1 && progressCalls==before);
 LibraryListActivity xtc;allocation=0;xtc.openBookStatistics("book.xtc","Title");
 assert(xtc.pushes==1 && xtc.child->cache=="xtc-cache" && xtc.child->progress==4200);
 LibraryListActivity xtcOom;allocation=0;failAt=1;xtcOom.openBookStatistics("book.xtc","Title");
 assert(xtcOom.pushes==0 && xtcOom.index.isOpen() && xtcOom.statisticsOpenFailed);failAt=0;
 LibraryListActivity noIndex;noIndex.index.opened=false;allocation=0;noIndex.openBookStatistics("book.epub","Title");
 assert(noIndex.pushes==1);noIndex.callback({});assert(!noIndex.index.isOpen());
 LibraryListActivity dirty;allocation=0;dirty.openBookStatistics("book.epub","Title");library::dirty=true;
 dirty.callback({});assert(dirty.index.isOpen() && dirty.rebuilds==1 && dirty.resets==1 && !dirty.filterFailed);
 LibraryListActivity rebuildFailed;allocation=0;library::dirty=false;rebuildFailed.openBookStatistics("book.epub","Title");
 library::dirty=true;rebuildFailed.rebuildOk=false;rebuildFailed.callback({});
 assert(rebuildFailed.filterFailed && rebuildFailed.filteredCount==0 && rebuildFailed.closeCount==1);library::dirty=false;
 LibraryListActivity badIndex;badIndex.index.openOk=false;allocation=0;badIndex.openBookStatistics("book.epub","Title");
 assert(badIndex.pushes==1);badIndex.callback({});assert(!badIndex.index.isOpen() && badIndex.filterFailed && badIndex.filteredCount==0 && badIndex.pinnedTotal==0 && badIndex.closeCount==1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / 'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20','-DCROSSPOINT_EXPERIMENTAL_COMPLETION_MOVE',str(directory / 'test.cpp'),'-o',str(directory / 'test')],check=True)
    subprocess.run([str(directory / 'test')],check=True)
print('Library statistics launch and index recovery paths passed')
