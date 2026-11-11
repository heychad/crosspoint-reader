"""Exercise confirmation and cache retry without retaining the reader's files."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/activities/reader/EpubReaderActivity.cpp").read_text()
start = source.index("void EpubReaderActivity::confirmCacheReset(")
end = source.index("void EpubReaderActivity::onReaderMenuConfirm(", start)
cache = (root / "src/activities/settings/ClearCacheActivity.cpp").read_text()
clear_start = cache.index("void ClearCacheActivity::clearBookCache()")
clear_end = cache.index("void ClearCacheActivity::loop()", clear_start)
manager = (root / "src/activities/ActivityManager.cpp").read_text()
exit_start = manager.index("void ActivityManager::exitActivity(")
exit_end = manager.index("void ActivityManager::replaceActivity(", exit_start)
code = r'''
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#define tr(x) #x
bool failAllocation=false,readerOpen=true,clearOk=true;
int calls=0;
struct ActivityResult{bool isCancelled=false;};
struct ConfirmationActivity{template<class...T>ConfirmationActivity(T&&...){}};
template<class T,class...A>std::unique_ptr<T>makeUniqueNoThrow(A&&...args){
 if(failAllocation)return nullptr;return std::make_unique<T>(std::forward<A>(args)...);}
namespace readingstats{bool clearReaderCache(const std::string& path){assert(!readerOpen&&path=="cache");++calls;return clearOk;}}
struct Popup{std::function<void(int)>done;template<class F>void showMessage(const char*,const char*,const char**,int,int,F f){done=f;}};
struct ClearCacheActivity{
 std::string bookPath,cachePath;enum{WARNING,CLEARING,FAILED};int state=CLEARING,updates=0;bool bookClearPending=false;Popup confirmPopup;
 ClearCacheActivity(int,int,std::string b,std::string c):bookPath(b),cachePath(c){}
 void requestUpdate(){++updates;}void requestUpdateAndWait(){}
 void clearBookCache();
};
struct RenderLock{};
struct HeaderBackTapTarget{static void clear(){}};
struct ReaderOwner{void onExit(){assert(readerOpen);}~ReaderOwner(){readerOpen=false;}};
struct ActivityManager{
 std::unique_ptr<ReaderOwner>currentActivity=std::make_unique<ReaderOwner>();
 std::unique_ptr<ClearCacheActivity>pending;int reopened=0;
 void exitActivity(const RenderLock&);
 void replaceActivity(std::unique_ptr<ClearCacheActivity>p){assert(readerOpen);pending=std::move(p);}
 void goToReader(const std::string&path){assert(!readerOpen&&path=="book.epub");++reopened;}
}activityManager;
class EpubReaderActivity{public:
 struct Epub{std::string cache="cache";const std::string&getCachePath(){return cache;}};
 std::shared_ptr<Epub>epub=std::make_shared<Epub>();
 int renderer=0,mappedInput=0,footnoteDepth=0,updates=0,menus=0,dialogs=0;std::string bookPath="book.epub";
 std::function<void(const ActivityResult&)>callback;
 void requestUpdate(){++updates;}void openReaderMenu(){++menus;}
 template<class F>void startActivityForResult(std::unique_ptr<ConfirmationActivity>,F f){++dialogs;callback=f;}
 void complete(bool cancel){auto done=callback;done({cancel});}
 void confirmCacheReset();
};
''' + source[start:end] + manager[exit_start:exit_end] + cache[clear_start:clear_end] + r'''
int main(){
 EpubReaderActivity reader;
 reader.confirmCacheReset();assert(reader.dialogs==1&&calls==0&&!activityManager.pending);
 reader.complete(true);assert(reader.menus==1&&calls==0&&!activityManager.pending);
 EpubReaderActivity linked;linked.footnoteDepth=1;linked.confirmCacheReset();linked.complete(false);
 assert(!activityManager.pending&&calls==0);
 failAllocation=true;reader.confirmCacheReset();assert(reader.updates==1&&reader.dialogs==1);
 failAllocation=false;reader.confirmCacheReset();failAllocation=true;reader.complete(false);
 assert(!activityManager.pending&&calls==0);failAllocation=false;
 reader.confirmCacheReset();reader.complete(false);assert(activityManager.pending&&calls==0&&readerOpen);
 // The manager closes/destroys the reader before entering the replacement.
 activityManager.exitActivity(RenderLock{});assert(!readerOpen);auto&clear=*activityManager.pending;
 clearOk=false;clear.clearBookCache();assert(calls==1&&activityManager.reopened==0&&clear.state==ClearCacheActivity::WARNING);
 clear.confirmPopup.done(1);assert(clear.bookClearPending&&clear.state==ClearCacheActivity::CLEARING);
 clearOk=true;clear.clearBookCache();assert(calls==2&&activityManager.reopened==1);
 clearOk=false;clear.clearBookCache();clear.confirmPopup.done(0);assert(activityManager.reopened==2);
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / "test.cpp").write_text(code)
    subprocess.run(["c++", "-std=c++20", str(directory / "test.cpp"), "-o", str(directory / "test")], check=True)
    subprocess.run([str(directory / "test")], check=True)
print("EPUB cache reset defers clearing until reader replacement, preserves cancellation, and retries failures")
