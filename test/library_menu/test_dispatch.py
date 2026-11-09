"""Exercise menu, shortcut, and real manager allocation branches together."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
def section(path, start, end):
    source = (root / path).read_text()
    begin = source.index(start)
    return source[begin:source.index(end, begin + len(start))]
menu = section('src/activities/reader/EpubReaderActivity.cpp',
               '    case EpubReaderMenuActivity::MenuAction::LIBRARY:',
               '    case EpubReaderMenuActivity::MenuAction::')
shortcut = section('src/activities/reader/ReaderGestureInput.cpp',
                   '    case Action::Library:', '    case Action::')
manager = section('src/activities/ActivityManager.cpp',
                  'void ActivityManager::goToLibrary()', '\nvoid ActivityManager::')
code = r'''
#include <cassert>
#include <memory>
#include <utility>
#include "personalization/ReaderShortcuts.h"
#define LOG_ERR(...) ((void)0)
using readershortcuts::Action;
bool canAllocate=true;
struct LibraryListActivity {LibraryListActivity(int&,int&) {}};
template<class T,class... A> std::unique_ptr<T> makeUniqueNoThrow(A&&... a){
 if(!canAllocate)return {};return std::make_unique<T>(std::forward<A>(a)...);
}
struct ActivityManager {
 int renderer=0,mappedInput=0,replacements=0;bool pending=false;
 void replaceActivity(std::unique_ptr<LibraryListActivity> activity){++replacements;pending=bool(activity);}
 void goToLibrary();
};
''' + manager + r'''
ActivityManager activityManager;
struct EpubReaderMenuActivity {enum class MenuAction {LIBRARY};};
struct ReaderActivity {
 bool handleReaderShortcut(Action action){switch(action){
''' + shortcut + r'''
 default:return false;}}
};
struct EpubReaderActivity:ReaderActivity {
 int updates=0;void requestUpdate(){++updates;}
 void dispatch(EpubReaderMenuActivity::MenuAction action){switch(action){
''' + menu + r'''
 }}
};
int main(){
 EpubReaderActivity reader;reader.dispatch(EpubReaderMenuActivity::MenuAction::LIBRARY);
 assert(activityManager.replacements==1 && activityManager.pending && reader.updates==1);
 canAllocate=false;activityManager.pending=false;
 reader.dispatch(EpubReaderMenuActivity::MenuAction::LIBRARY);
 assert(activityManager.replacements==1 && !activityManager.pending && reader.updates==2);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-I', str(root / 'src'), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Library menu uses the checked manager allocation path and repaints on failure')
