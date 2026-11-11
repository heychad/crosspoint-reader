"""Exercise production XTC menu actions, navigation, and allocation failures."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
menu = (root / "src/activities/reader/XtcReaderMenuActivity.cpp").read_text()
reader = (root / "src/activities/reader/XtcReaderActivity.cpp").read_text()
header = (root / "src/activities/reader/XtcReaderMenuActivity.h").read_text()

def method(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for end in range(opening, len(text)):
        depth += (text[end] == "{") - (text[end] == "}")
        if depth == 0:
            return text[start:end+1]
    raise ValueError(signature)

menu_methods = "\n".join(method(menu, signature) for signature in (
    "size_t XtcReaderMenuActivity::buildItems(", "void XtcReaderMenuActivity::cancel()",
    "bool XtcReaderMenuActivity::handleHomeGesture()", "bool XtcReaderMenuActivity::handleButtons()",
    "void XtcReaderMenuActivity::activateIndex("))
reader_methods = "\n".join(method(reader, signature) for signature in (
    "void XtcReaderActivity::openChapterSelection(", "void XtcReaderActivity::openPercentSelection()",
    "void XtcReaderActivity::openReaderMenu()", "bool XtcReaderActivity::handleFormatInput()"))
actions = re.search(r'enum class MenuAction\s*\{(.*?)\};', header, re.S).group(1)
ids = sorted(set(re.findall(r'StrId::(STR_[A-Z0-9_]+)', menu_methods)))
code = r'''
#include <array>
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <climits>
#include "activities/reader/XtcReaderNavigation.h"
#include "personalization/ReaderShortcuts.h"
#define LOG_ERR(...) ((void)0)
enum class StrId {__IDS__};
struct MenuResult {int action=-1;};struct PercentResult{int percent;};struct PageResult{uint32_t page;};
struct ActivityResult {bool isCancelled=false;std::variant<std::monostate,MenuResult,PercentResult,PageResult>data;};
struct RenderLock {};
struct Input {enum class Button{Back,Confirm};bool back=false,confirm=false;bool hasTouch(){return true;}
 bool wasReleased(Button b){return b==Button::Back?back:confirm;}};
using MappedInputManager=Input;
namespace ReaderUtils {bool touch=false;bool isTouchMenuGesture(int&,Input&){return touch;}}
namespace haptic_feedback {int events=0;void touchAction(){++events;}}
struct {uint8_t touchReaderControls=1;int saves=0;void saveToFile(){++saves;}}SETTINGS;
struct XtcReaderMenuActivity {
 static constexpr int kind=1,MAX_ITEMS=8;
 enum class MenuAction {__ACTIONS__};struct MenuItem{MenuAction action;StrId label;};
 std::array<MenuItem,MAX_ITEMS>items{};size_t count=0;bool finished=false;int updates=0;
 struct {int selected=0;}nav;Input mappedInput;struct {void clearTapFlash(){}}app;
 ActivityResult result;
 XtcReaderMenuActivity(int&,Input&,std::string,bool chapters,bool touch){count=buildItems(items,chapters,touch);}
 static size_t buildItems(std::array<MenuItem,MAX_ITEMS>&,bool,bool);
 void setResult(ActivityResult r){result=r;}void setResult(MenuResult r){result.data=r;}
 void finish(){finished=true;}void requestUpdate(){++updates;}
 void cancel();bool handleHomeGesture();bool handleButtons();void activateIndex(int);
};
''' + menu_methods + r'''
struct FakeXtc {uint32_t pages=100;bool chapters=true;uint32_t getPageCount(){return pages;}
 bool hasChapters(){return chapters;}std::vector<int>getChapters(){return chapters?std::vector<int>{0}:std::vector<int>{};}
 std::string getTitle(){return "Book";}};
struct XtcReaderChapterSelectionActivity {static constexpr int kind=2;template<class...T>XtcReaderChapterSelectionActivity(T&&...){}};
struct EpubReaderPercentSelectionActivity {static constexpr int kind=3;template<class...T>EpubReaderPercentSelectionActivity(T&&...){}};
enum class SettingsSection{Controls};
struct SettingsActivity {static constexpr int kind=4;template<class...T>SettingsActivity(T&&...){}};
int failKind=0;
template<class T,class...A>std::unique_ptr<T>makeUniqueNoThrow(A&&...args){
 if(failKind==T::kind)return nullptr;return std::make_unique<T>(std::forward<A>(args)...);
}
struct XtcReaderActivity {
 std::shared_ptr<FakeXtc>xtc=std::make_shared<FakeXtc>();uint32_t currentPage=23;
 int renderer=0,lastKind=0,updates=0,quick=0,home=0;Input mappedInput;
 readershortcuts::Action last=readershortcuts::Action::Default;
 std::function<void(const ActivityResult&)>callback;
 void requestUpdate(){++updates;}int getProgressPercent(){return 24;}
 bool handleReaderShortcut(readershortcuts::Action a){last=a;return true;}
 void openQuickActions(){++quick;}void onGoHome(){++home;}
 template<class T,class F>void startActivityForResult(std::unique_ptr<T>,F f){lastKind=T::kind;callback=std::move(f);}
 void complete(ActivityResult result){auto done=callback;done(result);}
 void openChapterSelection(bool returnToMenu=false);void openPercentSelection();void openReaderMenu();bool handleFormatInput();
};
''' + reader_methods + r'''
int main(){
 using M=XtcReaderMenuActivity;using A=M::MenuAction;
 int r=0;Input in;
 for(bool chapters:{false,true})for(bool touch:{false,true}){
  M menu(r,in,"Book",chapters,touch);assert(menu.count==6+int(chapters)+int(touch));
  bool hasChapter=false;for(size_t i=0;i<menu.count;++i)hasChapter|=menu.items[i].action==A::Chapters;
  assert(hasChapter==chapters);menu.activateIndex(-1);menu.activateIndex(menu.count);assert(!menu.finished);
  if(touch){menu.activateIndex(menu.count-1);assert(!menu.finished&&menu.updates==1);}
  menu.handleHomeGesture();assert(menu.finished&&menu.result.isCancelled);
 }
 XtcReaderActivity reader;reader.xtc->chapters=false;reader.openReaderMenu();assert(reader.lastKind==M::kind);
 reader.lastKind=0;reader.mappedInput.confirm=true;assert(reader.handleFormatInput()&&reader.lastKind==1);
 reader.mappedInput.confirm=false;ReaderUtils::touch=true;assert(reader.handleFormatInput()&&haptic_feedback::events==1);
 ReaderUtils::touch=false;
 reader.complete({true,{}});assert(reader.currentPage==23);
 reader.complete({false,MenuResult{int(A::Percent)}});assert(reader.lastKind==3);
 reader.complete({false,PercentResult{100}});assert(reader.currentPage==99);
 reader.openReaderMenu();reader.complete({false,MenuResult{int(A::Percent)}});
 reader.complete({true,{}});assert(reader.lastKind==1&&reader.currentPage==99);
 reader.xtc->chapters=true;reader.complete({false,MenuResult{int(A::Chapters)}});assert(reader.lastKind==2);
 reader.complete({true,{}});assert(reader.lastKind==1&&reader.currentPage==99);
 reader.complete({false,MenuResult{int(A::Chapters)}});reader.complete({false,PageResult{17}});assert(reader.currentPage==17);
 reader.openReaderMenu();reader.complete({false,MenuResult{int(A::Statistics)}});
 assert(reader.last==readershortcuts::Action::ReadingStatistics);
 reader.complete({false,MenuResult{int(A::QuickActions)}});assert(reader.quick==1);
 reader.complete({false,MenuResult{int(A::Library)}});assert(reader.last==readershortcuts::Action::Library);
 reader.complete({false,MenuResult{int(A::Home)}});assert(reader.home==1);
 failKind=3;reader.openPercentSelection();assert(reader.lastKind==1&&reader.currentPage==17);
 failKind=2;reader.openChapterSelection(true);assert(reader.lastKind==1);
 failKind=4;reader.complete({false,MenuResult{int(A::Controls)}});assert(reader.lastKind==1);
 failKind=1;const int updates=reader.updates;reader.openReaderMenu();assert(reader.updates==updates+1);
 for(uint32_t pages:{1u,2u,3u,101u,UINT32_MAX}){
  uint32_t previous=0;
  for(int p=0;p<=100;++p){auto page=xtcPageForPercent(p,pages);assert(page<pages&&page>=previous);previous=page;}
  assert(xtcPageForPercent(-1,pages)==0&&xtcPageForPercent(INT_MAX,pages)==pages-1);
 }
 assert(xtcPageForPercent(100,0)==0&&xtcPageForPercent(50,100)==49);
}
'''
code = code.replace("__IDS__", ",".join(ids)).replace("__ACTIONS__", actions)
with tempfile.TemporaryDirectory() as directory:
    directory=Path(directory)
    (directory/"test.cpp").write_text(code)
    subprocess.run(["c++","-std=c++20","-I",str(root/"src"),str(directory/"test.cpp"),"-o",str(directory/"test")],check=True)
    subprocess.run([str(directory/"test")],check=True)
print("XTC menu, cancel paths, allocation failures, and percentage bounds passed")
