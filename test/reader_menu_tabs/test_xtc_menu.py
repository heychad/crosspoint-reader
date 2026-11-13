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
    "void XtcReaderActivity::openReaderMenu()", "void XtcReaderActivity::confirmCacheReset(", "bool XtcReaderActivity::handleFormatInput()"))
reader_methods += "\n#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)\n" + "\n".join(
    method(reader, signature) for signature in (
        "bool XtcReaderActivity::completionActionAvailable(", "void XtcReaderActivity::toggleCompletion()")) + "\n#endif\n"
reader_methods += "\n#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)\n" + method(
    (root / "src/activities/reader/ReaderStatisticsActions.cpp").read_text(), "void ReaderActivity::confirmStatisticsReset(").replace("ReaderActivity::", "XtcReaderActivity::") + "\n#endif\n"
actions = re.search(r'enum class MenuAction\s*\{(.*?)\};', header, re.S).group(1)
ids = sorted(set(re.findall(r"STR_[A-Z0-9_]+", menu_methods + reader_methods)))
code = r'''
#include <array>
#include <atomic>
#include "util/ReadFolder.h"
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
struct Language{StrId get(StrId value){return value;}}I18N;
namespace statsbackup {enum class BookResetScope{Statistics,ReadingPace};enum class BookRestoreResult{Restored,Unavailable,Busy,BackupFailed,WriteUncertain,RecoveryRequired};}
#define tr(key) StrId::key
struct MenuResult {int action=-1;};struct PercentResult{int percent;};struct PageResult{uint32_t page;};
struct ActivityResult {bool isCancelled=false;std::variant<std::monostate,MenuResult,PercentResult,PageResult>data;};
struct RenderLock {};
struct Input {enum class Button{Back,Confirm};bool back=false,confirm=false;bool hasTouch(){return true;}
 bool wasReleased(Button b){return b==Button::Back?back:confirm;}};
using MappedInputManager=Input;
namespace ReaderUtils {bool touch=false;bool isTouchMenuGesture(int&,Input&){return touch;}}
namespace haptic_feedback {int events=0;void touchAction(){++events;}}
struct CrossPointSettings {enum {XTC_STATUS_BAR_HIDE,XTC_STATUS_BAR_BOTTOM,XTC_STATUS_BAR_TOP};};
struct UITheme {static UITheme& getInstance(){static UITheme t;return t;}int getStatusBarHeight()const{return 24;}};
struct {struct Spec{int xtcMode;};Spec statusBarSpec()const{return {CrossPointSettings::XTC_STATUS_BAR_HIDE};}uint8_t touchReaderControls=1;bool moveFinishedToReadFolder=false;unsigned char trackReadingStats=1,readingIdleTimeThresholdUnits=30;int saves=0;void saveToFile(){++saves;}}SETTINGS;
struct XtcReaderMenuActivity {
 static constexpr int kind=1,MAX_ITEMS=12;
 enum class MenuAction {__ACTIONS__};struct MenuItem{MenuAction action;StrId label;};
 std::array<MenuItem,MAX_ITEMS>items{};size_t count=0;bool finished=false;int updates=0;
 struct {int selected=0;}nav;Input mappedInput;struct {void clearTapFlash(){}}app;
 ActivityResult result;
 XtcReaderMenuActivity(int&,Input&,std::string,bool chapters,bool touch,bool known=false,bool done=false){count=buildItems(items,chapters,touch,known,done);}
 static size_t buildItems(std::array<MenuItem,MAX_ITEMS>&,bool,bool,bool=false,bool=false);
 void setResult(ActivityResult r){result=r;}void setResult(MenuResult r){result.data=r;}
 void finish(){finished=true;}void requestUpdate(){++updates;}
 void cancel();bool handleHomeGesture();bool handleButtons();void activateIndex(int);
};
''' + menu_methods + r'''
struct FakeXtc {std::string cache="cache";const std::string&getCachePath(){return cache;}bool clearOk=true;int clears=0;bool clearImageCache(){++clears;return clearOk;}uint32_t pages=100;bool chapters=true;uint32_t getPageCount(){return pages;}
 bool hasChapters(){return chapters;}std::vector<int>getChapters(){return chapters?std::vector<int>{0}:std::vector<int>{};}
 std::string getTitle(){return "Book";}};
struct XtcReaderChapterSelectionActivity {static constexpr int kind=2;template<class...T>XtcReaderChapterSelectionActivity(T&&...){}};
struct EpubReaderPercentSelectionActivity {static constexpr int kind=3;template<class...T>EpubReaderPercentSelectionActivity(T&&...){}};
enum class SettingsSection{Controls};
struct SettingsActivity {static constexpr int kind=4;template<class...T>SettingsActivity(T&&...){}};
struct ConfirmationActivity {static constexpr int kind=5;template<class...T>ConfirmationActivity(T&&...){}};
int failKind=0;
template<class T,class...A>std::unique_ptr<T>makeUniqueNoThrow(A&&...args){
 if(failKind==T::kind)return nullptr;return std::make_unique<T>(std::forward<A>(args)...);
}
struct XtcReaderActivity {
 std::atomic<bool>pageRendered{true};std::string bookPath="/Books/book.xtc";
 enum class StatisticsResetPrompt{Confirm,Retry,Recovery};
 void confirmStatisticsReset(StatisticsResetPrompt mode=StatisticsResetPrompt::Confirm, statsbackup::BookResetScope scope=statsbackup::BookResetScope::Statistics);
 std::atomic<bool>pendingStatsLoadWarning{false};
 struct Stats{statsbackup::BookResetScope lastScope=statsbackup::BookResetScope::Statistics;int resets=0;bool unavailableAtStart(){return false;}
 statsbackup::BookRestoreResult next=statsbackup::BookRestoreResult::Restored;
 statsbackup::BookRestoreResult resetBookStatistics(const std::string&,const std::string&,bool,uint16_t,bool,statsbackup::BookResetScope scope){++resets;lastScope=scope;return next;}
 bool known=true,done=false;bool completionStatus(bool& value)const{value=done;return known;}}readingStats;
 int completionEdits=0;bool completionTarget=false;
 bool completionActionAvailable(bool&)const;void toggleCompletion();
 void handleCompletionAttempt(bool recovery,bool desired){assert(!recovery);++completionEdits;completionTarget=desired;}
 std::shared_ptr<FakeXtc>xtc=std::make_shared<FakeXtc>();uint32_t currentPage=23;
 int renderer=0,lastKind=0,updates=0,quick=0,home=0,trackingRequests=0;Input mappedInput;
 readershortcuts::Action last=readershortcuts::Action::Default;
 std::function<void(const ActivityResult&)>callback;
 const std::string& getBookCachePath(){return xtc->getCachePath();}
 void requestUpdate(){++updates;}int getProgressPercent(){return 24;}
 bool handleReaderShortcut(readershortcuts::Action a){last=a;return true;}
 void openReadingStatistics(bool toggle){assert(toggle);last=readershortcuts::Action::ReadingStatistics; ++trackingRequests;}
 void openQuickActions(){++quick;}void onGoHome(){++home;}
 template<class T,class F>void startActivityForResult(std::unique_ptr<T>,F f){lastKind=T::kind;callback=std::move(f);}
 void complete(ActivityResult result){auto done=callback;done(result);}
 void confirmCacheReset(bool retry=false);
 bool barHandled=false;int barCalls=0;
 bool handleStatusBarTap(int top,int bottom){assert(top==0 && bottom==0);++barCalls;return barHandled;}
 void openChapterSelection(bool returnToMenu=false);void openPercentSelection();void openReaderMenu();bool handleFormatInput();
};
''' + reader_methods + r'''
int main(){
 using M=XtcReaderMenuActivity;using A=M::MenuAction;
 int r=0;Input in;int extraReset=0;
#ifdef CROSSPOINT_EXPERIMENTAL_STATS_RESTORE
 extraReset=1;
#endif
 for(bool chapters:{false,true})for(bool touch:{false,true}){
  M menu(r,in,"Book",chapters,touch);assert(menu.count==8+extraReset+int(chapters)+int(touch));
  bool hasChapter=false;for(size_t i=0;i<menu.count;++i)hasChapter|=menu.items[i].action==A::Chapters;
  assert(hasChapter==chapters);menu.activateIndex(-1);menu.activateIndex(menu.count);assert(!menu.finished);
  if(touch){menu.activateIndex(menu.count-1);assert(!menu.finished&&menu.updates==1);}
  menu.handleHomeGesture();assert(menu.finished&&menu.result.isCancelled);
 }
 XtcReaderActivity reader;reader.xtc->chapters=false;reader.openReaderMenu();assert(reader.lastKind==M::kind);
 reader.lastKind=0;reader.mappedInput.confirm=true;assert(reader.handleFormatInput()&&reader.lastKind==1);
 reader.barHandled=true;reader.lastKind=0;
 assert(reader.handleFormatInput() && reader.lastKind==0);
 reader.barHandled=false;
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
 reader.complete({false,MenuResult{int(A::Tracking)}});assert(reader.trackingRequests==1);
 reader.complete({false,MenuResult{int(A::QuickActions)}});assert(reader.quick==1);
 reader.complete({false,MenuResult{int(A::Library)}});assert(reader.last==readershortcuts::Action::Library);
 reader.complete({false,MenuResult{int(A::Home)}});assert(reader.home==1);
 failKind=3;reader.openPercentSelection();assert(reader.lastKind==1&&reader.currentPage==17);
 failKind=2;reader.openChapterSelection(true);assert(reader.lastKind==1);
 failKind=4;reader.complete({false,MenuResult{int(A::Controls)}});assert(reader.lastKind==1);
 failKind=1;const int updates=reader.updates;reader.openReaderMenu();assert(reader.updates==updates+1);
 failKind=0;reader.openReaderMenu();reader.complete({false,MenuResult{int(A::ClearCache)}});
 assert(reader.lastKind==5&&reader.xtc->clears==0);
 reader.complete({true,{}});assert(reader.lastKind==1&&reader.xtc->clears==0);
 reader.complete({false,MenuResult{int(A::ClearCache)}});reader.complete({false,{}});
 assert(reader.xtc->clears==1&&reader.currentPage==17);
 reader.xtc->clearOk=false;reader.confirmCacheReset();reader.complete({false,{}});
 assert(reader.xtc->clears==2&&reader.lastKind==5);
 reader.xtc->clearOk=true;reader.complete({false,{}});assert(reader.xtc->clears==3);
 failKind=5;int beforeFailure=reader.updates;reader.confirmCacheReset();
 assert(reader.updates==beforeFailure+1&&reader.xtc->clears==3);failKind=0;
 for(uint32_t pages:{1u,2u,3u,101u,UINT32_MAX}){
  uint32_t previous=0;
  for(int p=0;p<=100;++p){auto page=xtcPageForPercent(p,pages);assert(page<pages&&page>=previous);previous=page;}
  assert(xtcPageForPercent(-1,pages)==0&&xtcPageForPercent(INT_MAX,pages)==pages-1);
 }
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
 M completionMenu(r,in,"Book",true,true,true,false);assert(completionMenu.count==11+extraReset);
 bool found=false;for(size_t i=0;i<completionMenu.count;++i)if(completionMenu.items[i].action==A::Completion){
  found=true;assert(completionMenu.items[i].label==StrId::STR_MARK_FINISHED);}
 assert(found);
 M finishedMenu(r,in,"Book",true,true,true,true);
 for(size_t i=0;i<finishedMenu.count;++i)if(finishedMenu.items[i].action==A::Completion)
  assert(finishedMenu.items[i].label==StrId::STR_MARK_UNFINISHED);
 reader.openReaderMenu();reader.complete({false,MenuResult{int(A::Completion)}});
 assert(reader.completionEdits==1&&reader.completionTarget);
 reader.readingStats.done=true;reader.toggleCompletion();assert(reader.completionEdits==2&&!reader.completionTarget);
 reader.readingStats.known=false;reader.toggleCompletion();assert(reader.completionEdits==2);
 reader.readingStats.known=true;reader.readingStats.done=false;SETTINGS.moveFinishedToReadFolder=true;
 reader.toggleCompletion();assert(reader.completionEdits==2);
 reader.bookPath="/read/book.xtc";reader.toggleCompletion();assert(reader.completionEdits==3);
 reader.pageRendered=false;reader.toggleCompletion();assert(reader.completionEdits==3);
#endif
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
 const auto savedResetPage=reader.currentPage;
 reader.pageRendered=true;reader.openReaderMenu();reader.complete({false,MenuResult{int(A::ResetStatistics)}});
 assert(reader.lastKind==5&&reader.readingStats.resets==0);
 reader.complete({true,{}});assert(reader.lastKind==1&&reader.readingStats.resets==0);
 reader.complete({false,MenuResult{int(A::ResetStatistics)}});reader.complete({false,{}});
 assert(reader.readingStats.resets==1);
 reader.readingStats.next=statsbackup::BookRestoreResult::Unavailable;
 reader.confirmStatisticsReset();reader.complete({false,{}});assert(reader.readingStats.resets==2&&reader.lastKind==5);
 reader.readingStats.next=statsbackup::BookRestoreResult::Restored;reader.complete({false,{}});
 assert(reader.readingStats.resets==3);
 reader.readingStats.next=statsbackup::BookRestoreResult::RecoveryRequired;
 reader.confirmStatisticsReset();reader.complete({false,{}});assert(reader.readingStats.resets==4);
 int homes=reader.home;reader.complete({false,{}});assert(reader.home==homes+1&&reader.readingStats.resets==4);
 failKind=5;int resets=reader.readingStats.resets;reader.confirmStatisticsReset();assert(reader.readingStats.resets==resets);
 failKind=0;
 reader.readingStats.next=statsbackup::BookRestoreResult::Unavailable;
 reader.confirmStatisticsReset(XtcReaderActivity::StatisticsResetPrompt::Confirm,statsbackup::BookResetScope::ReadingPace);
 reader.complete({false,{}});assert(reader.readingStats.lastScope==statsbackup::BookResetScope::ReadingPace);
 reader.readingStats.next=statsbackup::BookRestoreResult::Restored;
 reader.complete({false,{}});assert(reader.readingStats.lastScope==statsbackup::BookResetScope::ReadingPace);
 assert(reader.currentPage==savedResetPage);
#endif
 assert(xtcPageForPercent(100,0)==0&&xtcPageForPercent(50,100)==49);
}
'''
code = code.replace("__IDS__", ",".join(ids)).replace("__ACTIONS__", actions)
with tempfile.TemporaryDirectory() as directory:
    directory=Path(directory)
    (directory/"test.cpp").write_text(code)
    for flags in ([], ["-DCROSSPOINT_EXPERIMENTAL_COMPLETION", "-DCROSSPOINT_EXPERIMENTAL_STATS_RESTORE"]):
        subprocess.run(["c++","-std=c++20",*flags,"-I",str(root/"src"),str(directory/"test.cpp"),"-o",str(directory/"test")],check=True)
        subprocess.run([str(directory/"test")],check=True)
print("XTC menu, cancel paths, allocation failures, and percentage bounds passed")
