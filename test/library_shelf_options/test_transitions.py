"""Exercise production confirmation transitions and their render-lock boundaries."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/activities/library/LibraryListActivity.cpp').read_text()

def method(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

code = r'''
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define tr(key) #key
bool locked=false,allocationOk=true;
struct RenderLock {
 bool held=true;
 template<class T> explicit RenderLock(T&){assert(!locked);locked=true;}
 void unlock(){assert(held && locked);held=false;locked=false;}
 ~RenderLock(){if(held){assert(locked);locked=false;}}
};
struct ActivityResult {bool isCancelled=true;};
struct ConfirmationActivity {ConfirmationActivity(int&,int&,const char*,const std::string&) {assert(locked);}};
template<class T,class... A>std::unique_ptr<T> makeUniqueNoThrow(A&&... a){
 return allocationOk?std::make_unique<T>(std::forward<A>(a)...):nullptr;
}
struct Index {
 bool opened=true,openOk=true;
 bool isOpen(){assert(locked);return opened;}
 void close(){assert(locked);opened=false;}
 bool open(const char*){assert(locked);opened=openOk;return opened;}
};
namespace library {const char* libraryIndexPath(){return "index";}}
struct RecentStore {
 bool removeByPath(const std::string&){assert(locked);return false;}
 void saveToFile(){assert(locked);}
} RECENT_BOOKS;
struct Gui {void drawPopup(int&,const char*){assert(locked);}} GUI;
struct Nav {int selected=1;bool followOnBuild=false;};
struct LibraryListActivity {
 Index index;int renderer=0,mappedInput=0,starts=0,releases=0,updates=0;Nav nav;
 std::function<void(const ActivityResult&)> callback;
 void swallowHeldReleases(){++releases;}
 void startActivityForResult(std::unique_ptr<ConfirmationActivity> screen,std::function<void(const ActivityResult&)> cb){
  assert(!locked && screen && !index.opened);++starts;callback=std::move(cb);
 }
 void promptDeleteBookByPath(const std::string&,const std::string&);
 void promptRemoveRecentBook(const std::string&,const std::string&);
 bool removeBookFile(const std::string&){assert(locked);return true;}
 void rebuildIndex(){assert(locked);}
 void resetAfterRebuild(){assert(locked);}
 void resolvePinned(){assert(locked);}
 void closeRouting(){}
 Nav& activeNav(){return nav;}
 int listCount(){return 3;}
 void requestUpdate(bool=false){++updates;}
};
'''
code += method('void LibraryListActivity::promptDeleteBookByPath(') + '\n'
code += method('void LibraryListActivity::promptRemoveRecentBook(') + r'''
int main(){
 LibraryListActivity a;a.promptDeleteBookByPath("/a.epub","A");
 assert(a.starts==1 && !a.index.opened);a.callback({true});assert(a.index.opened && !locked);
 LibraryListActivity b;allocationOk=false;b.promptDeleteBookByPath("/b.epub","B");
 assert(b.starts==0 && b.index.opened && !locked);
 LibraryListActivity c;c.promptRemoveRecentBook("/c.epub","C");
 assert(c.starts==0 && c.index.opened && !locked);
 allocationOk=true;c.promptRemoveRecentBook("/c.epub","C");
 assert(c.starts==1 && !c.index.opened);c.callback({true});assert(c.index.opened && !locked);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    source_path = Path(temporary) / 'test.cpp'
    binary_path = Path(temporary) / 'test'
    source_path.write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', str(source_path), '-o', str(binary_path)], check=True)
    subprocess.run([str(binary_path)], check=True)
print('Library confirmation transitions protect index and release lock before opening screens')
