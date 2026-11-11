"""Exercise production bookmark-clear confirmation and failure handling."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = (root / "src/activities/reader/EpubReaderActivity.cpp").read_text()
start = source.index("void EpubReaderActivity::confirmBookmarkClear(")
end = source.index("void EpubReaderActivity::confirmCacheReset(", start)
code = r'''
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#define tr(x) #x
bool locked=false,failAllocation=false,clearOk=true;int clears=0;
struct RenderLock{RenderLock(){assert(!locked);locked=true;}~RenderLock(){locked=false;}};
struct ActivityResult{bool isCancelled;};
struct ConfirmationActivity{template<class...T>ConfirmationActivity(T&&...){}};
template<class T,class...A>std::unique_ptr<T>makeUniqueNoThrow(A&&...args){
 if(failAllocation)return nullptr;return std::make_unique<T>(std::forward<A>(args)...);}
namespace BookmarkFile{bool clearForBook(const std::string&path){assert(locked&&path=="book.epub");++clears;return clearOk;}}
namespace BookmarkUtil{std::string getBookmarkPath(const std::string&){return "bookmarks.json";}}
struct HalStorage{enum class PathStatus{Present,Missing,Failed};PathStatus state=PathStatus::Present;
 PathStatus lookupPath(const char*){assert(locked);return state;}}Storage;
struct EpubReaderActivity{
 bool epub=true;int renderer=0,mappedInput=0,updates=0,menus=0,flags=0,dialogs=0,page=18;
 std::string bookPath="book.epub";std::vector<int>cachedBookmarks{1,2};
 std::function<void(const ActivityResult&)>callback;
 void confirmBookmarkClear(bool retry=false);void requestUpdate(){++updates;}void openReaderMenu(){++menus;}
 void updateBookmarkFlag(){assert(locked);++flags;}
 template<class F>void startActivityForResult(std::unique_ptr<ConfirmationActivity>,F f){++dialogs;callback=f;}
 void complete(bool cancel){auto f=callback;f({cancel});}
};
''' + source[start:end] + r'''
int main(){
 EpubReaderActivity reader;reader.confirmBookmarkClear();reader.complete(true);
 assert(clears==0&&reader.cachedBookmarks.size()==2&&reader.menus==1);
 clearOk=false;reader.confirmBookmarkClear();reader.complete(false);
 assert(clears==1&&reader.cachedBookmarks.size()==2&&reader.dialogs==3);
 Storage.state=HalStorage::PathStatus::Failed;reader.complete(false);
 assert(clears==2&&reader.cachedBookmarks.size()==2);
 clearOk=true;reader.complete(false);
 assert(clears==3&&reader.cachedBookmarks.empty()&&reader.flags==1&&reader.page==18);
 EpubReaderActivity partial;clearOk=false;Storage.state=HalStorage::PathStatus::Missing;
 partial.confirmBookmarkClear();partial.complete(false);assert(partial.cachedBookmarks.empty()&&partial.dialogs==2);
 EpubReaderActivity allocation;failAllocation=true;allocation.confirmBookmarkClear();assert(allocation.dialogs==0&&allocation.updates==1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / "test.cpp").write_text(code)
    subprocess.run(["c++", "-std=c++20", str(directory / "test.cpp"), "-o", str(directory / "test")], check=True)
    subprocess.run([str(directory / "test")], check=True)
print("Bookmark clear preserves cancellation, retries failures, and retains unknown native state")
