"""Exercise the production reader, shortcut, and manager file-browser handoff."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]


def section(path, start, end):
    source = (root / path).read_text()
    begin = source.index(start)
    return source[begin:source.index(end, begin + len(start))]


menu = section('src/activities/reader/EpubReaderActivity.cpp',
               '    case EpubReaderMenuActivity::MenuAction::FILE_BROWSER:',
               '    case EpubReaderMenuActivity::MenuAction::')
shortcut = section('src/activities/reader/ReaderGestureInput.cpp',
                   '    case Action::FileBrowser:', '    case Action::')
manager = section('src/activities/ActivityManager.cpp',
                  'void ActivityManager::goToFileBrowser(std::string path)', '\nvoid ActivityManager::')
code = r'''
#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include "personalization/ReaderShortcuts.h"
#define LOG_ERR(...) ((void)0)
using readershortcuts::Action;
bool canAllocate=true;
struct FileBrowserActivity {
 std::string path;FileBrowserActivity(int&,int&,std::string p):path(std::move(p)){}
};
template<class T,class... A> std::unique_ptr<T> makeUniqueNoThrow(A&&... a){
 if(!canAllocate)return {};return std::make_unique<T>(std::forward<A>(a)...);
}
struct ActivityManager {
 int renderer=0,mappedInput=0,replacements=0;std::string selected;bool pending=false;
 void replaceActivity(std::unique_ptr<FileBrowserActivity> activity){++replacements;pending=bool(activity);selected=activity->path;}
 void goToFileBrowser(std::string path={});
};
''' + manager + r'''
ActivityManager activityManager;
struct EpubReaderMenuActivity {enum class MenuAction {FILE_BROWSER};};
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
 EpubReaderActivity reader;reader.dispatch(EpubReaderMenuActivity::MenuAction::FILE_BROWSER);
 assert(activityManager.replacements==1 && activityManager.pending && activityManager.selected.empty() && reader.updates==1);
 activityManager.goToFileBrowser("/Books");
 assert(activityManager.replacements==2 && activityManager.selected=="/Books");
 canAllocate=false;activityManager.pending=false;
 reader.dispatch(EpubReaderMenuActivity::MenuAction::FILE_BROWSER);
 assert(activityManager.replacements==2 && !activityManager.pending && reader.updates==2);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-I', str(root / 'src'), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Browse Files opens the file browser and leaves the reader active on allocation failure')
