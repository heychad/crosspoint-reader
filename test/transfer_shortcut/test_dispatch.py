"""Exercise production transfer-menu, shortcut, and manager branches."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]


def section(path, start, end):
    source = (root / path).read_text()
    begin = source.index(start)
    return source[begin:source.index(end, begin + len(start))]


menu = section('src/activities/reader/EpubReaderActivity.cpp',
               '    case EpubReaderMenuActivity::MenuAction::FILE_TRANSFER:',
               '    case EpubReaderMenuActivity::MenuAction::')
base = section('src/activities/reader/ReaderGestureInput.cpp',
               '    case Action::FileTransfer:', '    case Action::')
forward = section('src/activities/reader/EpubReaderShortcuts.cpp',
                  '    case Action::ToggleFrontlight:', '    case Action::Screenshot:')
manager = section('src/activities/ActivityManager.cpp',
                  'void ActivityManager::goToFileTransfer()', '\nvoid ActivityManager::')
support_base = section('src/activities/reader/ReaderQuickActions.cpp',
                       'bool ReaderActivity::supportsReaderShortcut(', '\nvoid ReaderActivity::openQuickActions()')
support_epub = (root / 'src/activities/reader/EpubReaderShortcuts.cpp').read_text().split(
    'bool EpubReaderActivity::supportsReaderShortcut(', 1)[1]
support_epub = 'bool EpubReaderActivity::supportsReaderShortcut(' + support_epub

code = r'''
#include <cassert>
#include <memory>
#include <utility>
#include "personalization/ReaderShortcuts.h"
#define LOG_ERR(...) ((void)0)
using readershortcuts::Action;
using QuickLockTrigger = int;
bool canAllocate=true;
struct CrossPointWebServerActivity {CrossPointWebServerActivity(int&,int&) {}};
template<class T,class... A> std::unique_ptr<T> makeUniqueNoThrow(A&&... a){
 if(!canAllocate)return {};return std::make_unique<T>(std::forward<A>(a)...);
}
struct ActivityManager {
 int renderer=0,mappedInput=0,replacements=0;bool pending=false;
 void replaceActivity(std::unique_ptr<CrossPointWebServerActivity> activity){++replacements;pending=bool(activity);}
 void goToFileTransfer();
};
''' + manager + r'''
ActivityManager activityManager;
struct EpubReaderMenuActivity {enum class MenuAction {FILE_TRANSFER};};
struct Input {bool hasTouch()const{return true;}};
struct Light {bool present()const{return true;}bool hasColorTemperature()const{return true;}} Frontlight;
struct Tilt {bool isAvailable()const{return true;}} halTiltSensor;
struct ReaderActivity {
 Input mappedInput;bool supportsReaderShortcut(Action) const;
 int updates=0;void requestUpdate(){++updates;}
 bool handleReaderShortcut(Action action, QuickLockTrigger=0){switch(action){
''' + base + r'''
 default:return false;}}
};
struct EpubReaderActivity:ReaderActivity {
 bool supportsReaderShortcut(Action) const;
 bool handleReaderShortcut(Action action, QuickLockTrigger trigger=0){switch(action){
''' + forward + r'''
 default:return false;}}
 void dispatch(EpubReaderMenuActivity::MenuAction action){switch(action){
''' + menu + r'''
 }}
};
''' + support_base + '\n' + support_epub + r'''
int main(){
 EpubReaderActivity reader;assert(reader.supportsReaderShortcut(Action::FileTransfer));reader.dispatch(EpubReaderMenuActivity::MenuAction::FILE_TRANSFER);
 assert(activityManager.replacements==1 && activityManager.pending && reader.updates==1);
 canAllocate=false;activityManager.pending=false;
 reader.dispatch(EpubReaderMenuActivity::MenuAction::FILE_TRANSFER);
 assert(activityManager.replacements==1 && !activityManager.pending && reader.updates==2);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-I', str(root / 'src'), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Transfer shortcut reaches the existing mode chooser and repaints if allocation fails')
