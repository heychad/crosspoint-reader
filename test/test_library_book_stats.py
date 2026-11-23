"""Exercise the Library statistics launch and index recovery paths."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/library/LibraryListActivity.cpp').read_text()
start = source.index('void LibraryListActivity::openBookStatistics(')
end = source.index('\n#if FREEINK_CAP_TOUCH', start)
functions = source[start:end]
browser_source = (root / 'src/activities/home/FileBrowserActivity.cpp').read_text()
browser_menu = browser_source[browser_source.index('void FileBrowserActivity::showEntryActions('):
                              browser_source.index('void FileBrowserActivity::deleteSelected(')]
browser_input = browser_source[browser_source.index('bool FileBrowserActivity::handleCustomInput('):
                               browser_source.index('bool FileBrowserActivity::handleButtons(')]
stats_source = (root / 'src/activities/settings/ReadingStatsActivityClosedBook.cpp').read_text()
factory_start = stats_source.index('std::unique_ptr<ReadingStatsActivity> ReadingStatsActivity::forClosedBook(')
factory_end = stats_source.index('{', factory_start) + 1
factory_depth = 1
while factory_depth:
    factory_depth += (stats_source[factory_end] == '{') - (stats_source[factory_end] == '}')
    factory_end += 1
factory = stats_source[factory_start:factory_end]
assert 'GlobalReadingStats::loadReadOnly()' in factory
assert 'BookReadingStats::LoadMode::ReadOnly' in factory
menu = source[source.index('void LibraryListActivity::showRecentBookOptions('):start]
assert 'isStoreRow ? 5 : 4' in menu
assert 'case 3:' in menu and 'case 4:' in menu and menu.count('openBookStatistics(path, title)') == 2
assert menu.index('case 1:') < menu.index('promptRemoveRecentBook(path, title)') < menu.index('case 2:')
loop = source[source.index('void LibraryListActivity::loop()'):]
assert loop.index('UiTabListActivity::loop();') < loop.index('if (statisticsOpenFailed)') < loop.index('optionPopup.showMessage')
error_popup = loop[loop.index('if (statisticsOpenFailed)'):loop.index('if (!gridEnabled()')]
assert error_popup.index('RenderLock lock(*this);') < error_popup.index('optionPopup.showMessage')
code = r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#define LOG_ERR(...) ((void)0)
using GfxRenderer=int;
struct MappedInputManager {
 enum class Button{Back};bool wasReleased(Button) const {return false;}int getHeldTime() const {return 0;}
};
constexpr int GO_HOME_MS=1000;
struct BookReadingStats {enum class LoadMode{ReadOnly};bool writable=true;};
bool globalWritable=true;
struct GlobalReadingStats {bool writable=true;static GlobalReadingStats loadReadOnly(){return {globalWritable};}};
struct ActivityResult{};
struct HalStorage {enum class PathStatus{Present,Missing,Failed};};
struct StorageType {HalStorage::PathStatus status=HalStorage::PathStatus::Present;HalStorage::PathStatus lookupPath(const char*){return status;}} Storage;
namespace library {bool dirty=false;bool isLibraryIndexDirty(){return dirty;}const char* libraryIndexPath(){return "index";}}
struct Index {bool opened=true,openOk=true;bool isOpen(){return opened;}void close(){opened=false;}bool open(const char*){opened=openOk;return opened;}bool ranksDegraded(){return false;}};
constexpr int STR_LIBRARY_REBUILDING=1;
enum class StrId{STR_OPEN,STR_DELETE,STR_RENAME,STR_READING_STATISTICS,STR_FILENAME};
constexpr const char* STR_OK_BUTTON="OK";
constexpr const char* STR_READING_STATISTICS="Reading statistics";
constexpr const char* STR_LIBRARY_STATS_OPEN_ERROR="Could not open book statistics. Try again.";
int tr(int value){return value;}
const char* tr(const char* value){return value;}
struct Gui {void drawPopup(int&,int){}} GUI;
struct {bool libraryHideFinishedBooks=false;} SETTINGS;
struct {int count=4;int getCount() const{return count;}} RECENT_BOOKS;
namespace FsHelpers {
bool hasReflowableBookExtension(const std::string& p){return p.ends_with(".epub") || p.ends_with(".md") || p.ends_with(".txt");}
bool hasEpubExtension(const std::string& p){return p.ends_with(".epub");}
bool hasXtcExtension(const std::string& p){return p.ends_with(".xtc") || p.ends_with(".xtch");}
}
struct Epub {static std::string cachePathForFilePath(const std::string&,const char*){return "epub-cache";}};
struct Xtc {Xtc(const std::string&,const char*){} std::string getCachePath(){return "xtc-cache";}};
namespace readingstats {
bool foundValue=true,knownValue=true,trackingValue=true;int calls=0;
std::optional<bool> completed=false;
std::optional<bool> bookCompletion(const std::string&){return completed;}
BookReadingStats loadBook(const std::string&,const std::string&,bool& tracking,bool* found,BookReadingStats::LoadMode mode,bool* known){
 assert(mode==BookReadingStats::LoadMode::ReadOnly);++calls;tracking=trackingValue;*found=foundValue;*known=knownValue;return {};}
}
int progressCalls=0;int loadBookProgress(const std::string&){++progressCalls;return 42;}
struct ReadingStatsActivity {
 static std::unique_ptr<ReadingStatsActivity> forClosedBook(int&,MappedInputManager&,const std::string&,const std::string&);
 bool bookWritable,globalWritable;int progress;bool tracking;std::string cache,path;
 ReadingStatsActivity(int&,MappedInputManager&,const std::string&,const BookReadingStats& book,const GlobalReadingStats& global,int p,bool t,bool saved,const std::string& c,const std::string& b,void* live,bool closedDateEdit)
 :bookWritable(book.writable),globalWritable(global.writable),progress(p),tracking(t),cache(c),path(b){assert(saved && live==nullptr && closedDateEdit);}
};
int allocation=0,failAt=0;
template<class T,class... Args>std::unique_ptr<T> makeUniqueNoThrow(Args&&... args){
 ++allocation;if(failAt==allocation)return {};return std::make_unique<T>(std::forward<Args>(args)...);}
bool locked=false;
struct RenderLock {template<class T>explicit RenderLock(T&){assert(!locked);locked=true;}void unlock(){assert(locked);locked=false;}~RenderLock(){locked=false;}};
struct LibraryListActivity {
 bool history=false;int refreshes=0;
 bool historyView() const {return history;}void refreshViewIndex(){++refreshes;index.close();degraded=false;}
 Index index;int renderer=0;MappedInputManager mappedInput;bool filterFailed=false,statisticsOpenFailed=false,degraded=false,rebuildOk=true;int filteredCount=3,pinnedTotal=2,closeCount=0,updates=0,releases=0,pushes=0,rebuilds=0,resets=0;
 std::unique_ptr<ReadingStatsActivity> child;std::function<void(const ActivityResult&)> callback;
 void closeRouting(){++closeCount;}void requestUpdate(){++updates;}void swallowHeldReleases(){++releases;}
 bool rebuildIndex(){++rebuilds;return rebuildOk;}void resetAfterRebuild(){++resets;}
 void startActivityForResult(std::unique_ptr<ReadingStatsActivity> activity,std::function<void(const ActivityResult&)> cb){assert(!locked);++pushes;child=std::move(activity);callback=std::move(cb);}
 void openBookStatistics(const std::string&,const std::string&);void restoreIndexAfterStatistics(bool,bool=false);
};
std::string utf8ComposeNfc(const std::string& text){return text;}
struct OptionPopup {
 bool active=false;std::vector<StrId> options;std::string message;std::function<void(int)> select;
 bool isActive() const{return active;}
 void show(StrId,const StrId* items,int count,int,std::function<void(int)> callback){
  assert(locked);options.assign(items,items+count);select=std::move(callback);active=true;
 }
 void showMessage(const char*,const char* body,const char* const*,int,int,std::function<void(int)>){assert(locked);message=body;active=true;}
 bool handleInput(MappedInputManager&,const std::function<void()>&){return active;}
 void choose(int index){assert(active&&index<int(options.size()));active=false;select(index);}
};
struct FileBrowserActivity {
 enum class Mode {Books,PickFirmware,PickStatsBackup};Mode mode=Mode::Books;
 struct {int selected=0,top=0,follows=0;void follow(int){++follows;}} nav;
 std::string basepath="/Books";std::vector<std::string> files={"cover.png","book.epub"},afterReload;
 int renderer=0,updates=0,reloads=0,routingCloses=0;MappedInputManager mappedInput;
 OptionPopup optionPopup;bool statisticsOpenFailed=false;
 std::unique_ptr<ReadingStatsActivity> child;std::function<void(const ActivityResult&)> callback;
 int listCount()const{return files.size();}void requestUpdate(){++updates;}
 void activateSelected(){}void deleteSelected(){}void startRename(){}void closeRouting(){++routingCloses;}
 void loadFiles(){assert(locked);files=afterReload;++reloads;}
 void startActivityForResult(std::unique_ptr<ReadingStatsActivity> activity,std::function<void(const ActivityResult&)> cb){
  assert(!locked);child=std::move(activity);callback=std::move(cb);
 }
 void showEntryActions();void openSelectedBookStatistics();bool handleCustomInput();
};
''' + factory + '\n' + functions + '\n' + browser_menu + '\n' + browser_input + r'''
int main(){
 LibraryListActivity history;history.history=true;history.index.openOk=false;allocation=0;
 history.openBookStatistics("book.epub","Title");library::dirty=true;
 history.callback({});assert(!history.filterFailed&&history.refreshes==1&&history.rebuilds==0&&history.resets==1);
 library::dirty=false;
 LibraryListActivity ok;allocation=0;ok.openBookStatistics("book.epub","Title");
 assert(ok.pushes==1 && !ok.index.isOpen() && ok.child->bookWritable && ok.child->progress==4200 && ok.child->tracking);
 ok.callback({});assert(ok.index.isOpen() && ok.releases==1 && !ok.filterFailed && ok.resets==0);
 SETTINGS.libraryHideFinishedBooks=true;
 LibraryListActivity unchanged;unchanged.openBookStatistics("book.epub","Title");unchanged.callback({});
 assert(unchanged.index.isOpen() && unchanged.resets==0);
 LibraryListActivity finished;finished.openBookStatistics("book.epub","Title");readingstats::completed=true;finished.callback({});
 assert(finished.index.isOpen() && finished.resets==1 && finished.closeCount==1);
 readingstats::completed=false;SETTINGS.libraryHideFinishedBooks=false;
 LibraryListActivity removed;removed.openBookStatistics("book.epub","Title");--RECENT_BOOKS.count;removed.callback({});
 assert(removed.index.isOpen() && removed.resets==1 && removed.closeCount==1);
 LibraryListActivity missing;Storage.status=HalStorage::PathStatus::Missing;missing.openBookStatistics("book.epub","Title");
 assert(missing.pushes==0 && missing.index.isOpen() && missing.statisticsOpenFailed);Storage.status=HalStorage::PathStatus::Present;
 LibraryListActivity unreadable;Storage.status=HalStorage::PathStatus::Failed;unreadable.openBookStatistics("book.epub","Title");
 assert(!unreadable.child&&unreadable.statisticsOpenFailed&&unreadable.index.isOpen());Storage.status=HalStorage::PathStatus::Present;
 LibraryListActivity guarded;globalWritable=false;guarded.openBookStatistics("book.epub","Title");
 assert(guarded.child&&!guarded.child->globalWritable);globalWritable=true;
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
 LibraryListActivity textPlain;allocation=0;before=progressCalls;textPlain.openBookStatistics("book.txt","Text");
 assert(textPlain.child&&textPlain.child->progress==-1&&progressCalls==before);
 LibraryListActivity xtch;allocation=0;xtch.openBookStatistics("book.xtch","XTC-H");
 assert(xtch.child&&xtch.child->cache=="xtc-cache"&&xtch.child->progress==4200);
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

 FileBrowserActivity browser;browser.showEntryActions();assert(browser.optionPopup.options.size()==3);
 browser.optionPopup.active=false;browser.nav.selected=1;browser.showEntryActions();
 assert(browser.optionPopup.options.size()==4 && browser.optionPopup.options.back()==StrId::STR_READING_STATISTICS);
 browser.optionPopup.choose(3);assert(browser.child && browser.child->path=="/Books/book.epub");
 browser.callback({});assert(browser.reloads==0);
 Storage.status=HalStorage::PathStatus::Failed;browser.callback({});assert(browser.reloads==0);
 Storage.status=HalStorage::PathStatus::Missing;browser.afterReload={"cover.png"};browser.callback({});
 assert(browser.reloads==1 && browser.nav.selected==0 && browser.nav.follows==1 && browser.routingCloses==1);

 FileBrowserActivity failed;failed.nav.selected=1;failed.showEntryActions();failed.optionPopup.choose(3);
 assert(!failed.child && failed.statisticsOpenFailed && !failed.optionPopup.active);
 assert(failed.handleCustomInput() && failed.optionPopup.message==STR_LIBRARY_STATS_OPEN_ERROR && !failed.statisticsOpenFailed);
 Storage.status=HalStorage::PathStatus::Present;
 for (const auto mode : {FileBrowserActivity::Mode::PickFirmware,FileBrowserActivity::Mode::PickStatsBackup}) {
  FileBrowserActivity picker;picker.mode=mode;picker.nav.selected=1;picker.showEntryActions();assert(!picker.optionPopup.active);
 }
 FileBrowserActivity folder;folder.files={"Subfolder/"};folder.showEntryActions();assert(folder.optionPopup.options.size()==2);
 FileBrowserActivity root;root.basepath="/";root.files={"book.xtch"};root.showEntryActions();root.optionPopup.choose(3);
 assert(root.child && root.child->path=="/book.xtch");
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / 'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20','-DCROSSPOINT_EXPERIMENTAL_COMPLETION_MOVE',str(directory / 'test.cpp'),'-o',str(directory / 'test')],check=True)
    subprocess.run([str(directory / 'test')],check=True)
print('Library and File Browser statistics launch, list refresh, and recovery paths passed')
