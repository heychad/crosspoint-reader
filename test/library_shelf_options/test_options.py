"""Exercise real shelf option routing without changing storage."""
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

assert 'onRowLongPress(selectedEntry())' in method('bool LibraryListActivity::handleButtons(')
code = r'''
#include <cassert>
#include <functional>
#include <string>
#include <vector>
#define LOG_ERR(...) ((void)0)
#define tr(key) #key
bool locked=false;
struct RenderLock {
 template<class T> explicit RenderLock(T&){assert(!locked);locked=true;}
 ~RenderLock(){assert(locked);locked=false;}
};
struct Popup {
 std::vector<std::string> labels;std::function<void(int)> action;
 void show(const char*,const char*,const char* const* labelsIn,int count,int,std::function<void(int)> cb){
  assert(locked);labels.assign(labelsIn,labelsIn+count);action=std::move(cb);
 }
};
struct App {int clears=0;void clearTapFlash(){assert(locked);++clears;}};
struct Index {bool opened=true;void close(){assert(locked);opened=false;}};
bool isRecentSort(int sort){return sort==0;}
struct LibraryListActivity {
 bool groupsCollapsed=false,canGroup=true,pathOk=true,titleOk=true;int sortOrder=1,count=3,updates=0,releases=0;
 std::string query,opened,stats,deleted;int grouped=-1,activated=-1,recent=-1;
 App app;Popup optionPopup;Index index;
 int listCount()const{return count;}
 bool groupable()const{return canGroup;}
 bool rowPathFor(int i,std::string& out){assert(locked);out="/book-"+std::to_string(i)+".epub";return pathOk;}
 bool rowTextFor(int i,std::string& title,std::string& author){assert(locked);title="Title "+std::to_string(i);author="Author";return titleOk;}
 void activateIndex(int i){activated=i;}
 void showRecentBookOptions(int i){recent=i;}
 void showShelfBookOptions(int);
 void onRowLongPress(int);
 void requestUpdate(){++updates;}
 void swallowHeldReleases(){++releases;}
 void openBookByPath(const std::string& p);
 void onSelectBook(const std::string& p){assert(!locked);opened=p;}
 void openBookStatistics(const std::string& p,const std::string& t){assert(!locked);stats=p+"|"+t;}
 void promptDeleteBookByPath(const std::string& p,const std::string& t){assert(!locked);deleted=p+"|"+t;}
 void collapseGroups(int i){assert(locked);grouped=i;}
};
'''
code += method('void LibraryListActivity::openBookByPath(') + '\n'
code += method('void LibraryListActivity::onRowLongPress(') + '\n'
code += method('void LibraryListActivity::showShelfBookOptions(') + r'''
int main(){
 LibraryListActivity view;
 view.onRowLongPress(-1);view.onRowLongPress(3);assert(!view.optionPopup.action);
 view.groupsCollapsed=true;view.onRowLongPress(2);assert(view.activated==2 && !view.optionPopup.action);
 view.groupsCollapsed=false;view.sortOrder=0;view.onRowLongPress(1);assert(view.recent==1);
 view.sortOrder=1;view.onRowLongPress(2);
 assert(view.optionPopup.labels==std::vector<std::string>({"STR_OPEN","STR_LIBRARY_BROWSE_GROUPS","STR_READING_STATISTICS","STR_DELETE"}));
 view.optionPopup.action(1);assert(view.grouped==2 && view.releases==1);
 view.optionPopup.action(2);assert(view.stats=="/book-2.epub|Title 2");
 view.optionPopup.action(0);assert(view.opened=="/book-2.epub");
 view.optionPopup.action(3);assert(view.deleted=="/book-2.epub|Title 2");
 view.query="search";view.onRowLongPress(1);
 assert(view.optionPopup.labels==std::vector<std::string>({"STR_OPEN","STR_READING_STATISTICS","STR_DELETE"}));
 view.optionPopup.action(1);assert(view.stats=="/book-1.epub|Title 1");
 view.optionPopup.action(2);assert(view.deleted=="/book-1.epub|Title 1");
 auto old=view.stats;view.optionPopup.action(-1);view.optionPopup.action(99);assert(view.stats==old);
 view.canGroup=false;view.query.clear();view.onRowLongPress(0);assert(view.optionPopup.labels.size()==3);
 view.optionPopup.action={};view.pathOk=false;view.onRowLongPress(0);assert(!view.optionPopup.action);
 view.pathOk=true;view.titleOk=false;view.onRowLongPress(0);assert(!view.optionPopup.action);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    source_path = Path(temporary) / 'test.cpp'
    binary_path = Path(temporary) / 'test'
    source_path.write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', str(source_path), '-o', str(binary_path)], check=True)
    subprocess.run([str(binary_path)], check=True)
print('Shelf options route open, groups, statistics, delete confirmation and invalid rows correctly')
