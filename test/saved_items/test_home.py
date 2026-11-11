"""Compile and exercise the complete production Home input method."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'src/activities/home/HomeActivity.cpp').read_text()
header=(ROOT/'src/activities/home/HomeActivity.h').read_text()
def method(source,signature):
    start=source.index(signature);end=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
code=r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>
#include "components/themes/lyra/CarouselLayout.h"
struct Rect {int x,y,width,height;};
constexpr int SMALL_FONT_ID=0;
struct Renderer {int getScreenHeight()const{return 800;}int getLineHeight(int)const{return 16;}};
struct Metrics {int homeTopPadding=56,homeCoverTileHeight=300,homeMenuTopOffset=16,buttonHintsHeight=40,menuSpacing=8;bool homeContinueReadingInMenu=false;int homeRecentBooksCount=1,contentSidePadding=12;};
struct Theme {int getMenuRowHeight(const Renderer&)const{return 64;}int getMenuTopInset()const{return 0;}} GUI;
struct UITheme {static UITheme& getInstance(){static UITheme theme;return theme;}const Metrics& getMetrics()const{static Metrics m;return m;}static Rect getContentArea(const Renderer&){return {0,0,480,800};}};
struct MappedInputManager {
 enum class Button {Back,Confirm,Up,Down,Left,Right};
 enum class SwipeDir {None,Up,Down,Left,Right};
 enum class RowTouch {None,Down,Tap};
 SwipeDir swipe=SwipeDir::None;int tapRow=-1;Button released=Button::Left;int swipeY=0,tapX=-1,tapY=-1;
 SwipeDir wasSwipe(int* x=nullptr,int* y=nullptr)const{if(x)*x=0;if(y)*y=swipeY;return swipe;}
 bool wasScreenTapped(int& x,int& y)const{x=tapX;y=tapY;return tapX>=0;}
 bool wasReleased(Button b)const{return b==released;}
 RowTouch colTouch(int&,int,int,int,int,int,int)const{return RowTouch::None;}
 RowTouch rowTouch(int& row,int,int,int count,int,int,int)const{if(tapRow<0||tapRow>=count)return RowTouch::None;row=tapRow;return RowTouch::Tap;}
};
struct ButtonNavigator {
 int direction=0;
 static int nextIndex(int index,int count){return (index+1)%count;}
 static int previousIndex(int index,int count){return (index+count-1)%count;}
 template<class F>void onNext(F f){if(direction==1)f();}
 template<class F>void onPrevious(F f){if(direction==-1)f();}
 template<class F>void onPressAndContinuous(std::initializer_list<MappedInputManager::Button>,F){}
};
struct CoverGrid {int selectedAction(const MappedInputManager&){return -1;}};
enum class HomeMenuItem {NONE,FILE_BROWSER,LIBRARY,SAVED_ITEMS,PLUGINS,OPDS_BROWSER,FILE_TRANSFER,SETTINGS_MENU};
std::string opened;
struct {void goToSavedItems(){opened="saved";}void goHome(){opened="home";}} activityManager;
struct ReadingStatsDate {};
struct ReadingStatsDateTime {ReadingStatsDate date;};
bool getCurrentLocalReadingStatsDateTime(ReadingStatsDateTime&){return false;}
uint32_t clockMs=0;
uint32_t millis(){return clockMs;}
namespace readingstats::deferredcompletion {
 enum class ExecuteMoveResult {Moved, Cancelled, Missing, Busy, Blocked, RecoveryRequired, Unavailable};
 ExecuteMoveResult result=ExecuteMoveResult::Missing;int attempts=0;bool show=false;
 ExecuteMoveResult executeOnePending(const ReadingStatsDate&,void(*callback)(void*),void* context){
   ++attempts;if(show && callback)callback(context);return result;
 }
}
namespace haptic_feedback {void touchAction(){}}
enum { STR_OK_BUTTON, STR_READING_STATISTICS, STR_READER_STATS_SAVE_FAILED, STR_RETRY, STR_SETTINGS_TITLE, STR_SETTINGS_RECOVERY_DETAILS, STR_FINISHED_MOVE_REVIEW };
const char* tr(int) {return "text";}
struct Notice { uint32_t pending() const {return 0;} void acknowledge(uint32_t) {} };
Notice& statsSaveNotice(){static Notice notice;return notice;}
Notice& settingsSaveNotice(){static Notice notice;return notice;}
struct CrossPointSettings { enum {LYRA_CAROUSEL=7}; };
struct Settings {int uiTheme=0;bool saveToFile(){return true;}} SETTINGS;
struct Popup {
 bool shown=false;
 bool isActive()const{return false;}
 template<class F>void showMessage(const char*,const char*,const char**,int,int,F){shown=true;}
 template<class F>bool handleInput(MappedInputManager&,F){return false;}
};
struct Book {std::string path;};
struct HomeActivity {
 bool libraryFirstOnHome = false;
 struct MenuPage {int top,first,count,capacity;};
 Renderer renderer;MappedInputManager mappedInput;ButtonNavigator buttonNavigator;Popup statsSavePopup;
 std::unique_ptr<CoverGrid> coverGridUi;
 std::vector<Book> recentBooks={{"/book.epub"}};
 bool hasPlugins=true,hasOpdsServers=false,hasContinueReading=true;
 int selectorIndex=1,updates=0;
 bool deferredMoveNeedsReview=false;
 bool deferredMoveRetryPending=false;uint32_t deferredMoveRetryAt=0;
 bool deferredMoveOverlayShown=false;
 static void showDeferredMove(void* context){static_cast<HomeActivity*>(context)->deferredMoveOverlayShown=true;}
 std::atomic<int> carouselCenterIndex{0};
 bool hasLibrarySlot()const{return hasPlugins||hasOpdsServers;}
 HomeMenuItem slotItem()const{return hasPlugins?HomeMenuItem::PLUGINS:hasOpdsServers?HomeMenuItem::OPDS_BROWSER:HomeMenuItem::NONE;}
 void requestUpdate(){++updates;}
 void onRecentBookSelected(const std::string& path){opened=path;}
 void onFileBrowserOpen(){opened="files";}
 void onLibraryOpen(){opened="library";}
 void onPluginsOpen(){opened="plugins";}
 void onOpdsBrowserOpen(){opened="opds";}
 void onFileTransferOpen(){opened="transfer";}
 void onSettingsOpen(){opened="settings";}
 int getMenuItemCount()const;
 MenuPage menuPage()const;
 void loop();
'''
code+=method(header,'  static HomeMenuItem indexToMenuItem(')+'\n};\n'
for signature in ['int HomeActivity::getMenuItemCount(', 'HomeActivity::MenuPage HomeActivity::menuPage(', 'void HomeActivity::loop(']:
    code+=method(source,signature)+'\n'
code+=r'''
int main(){
#if defined(CROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION)
 HomeActivity warning;
 warning.deferredMoveNeedsReview=true;
 warning.loop();
 assert(warning.statsSavePopup.shown && !warning.deferredMoveNeedsReview && warning.updates==1);
 HomeActivity retry;
 retry.deferredMoveRetryPending=true;retry.deferredMoveRetryAt=500;
 clockMs=499;retry.loop();
 assert(readingstats::deferredcompletion::attempts==0 && retry.deferredMoveRetryPending);
 clockMs=500;
 readingstats::deferredcompletion::result=readingstats::deferredcompletion::ExecuteMoveResult::Moved;
 retry.loop();assert(opened=="home" && !retry.deferredMoveRetryPending && readingstats::deferredcompletion::attempts==1);
 HomeActivity failed;
 failed.deferredMoveRetryPending=true;failed.deferredMoveRetryAt=500;
 readingstats::deferredcompletion::result=readingstats::deferredcompletion::ExecuteMoveResult::Unavailable;
 failed.loop();assert(failed.statsSavePopup.shown && !failed.deferredMoveRetryPending);
 HomeActivity rollover;
 rollover.deferredMoveRetryPending=true;rollover.deferredMoveRetryAt=0x20;
 clockMs=0xfffffff0u;const int before=readingstats::deferredcompletion::attempts;
 rollover.loop();assert(readingstats::deferredcompletion::attempts==before);
 clockMs=0x20;readingstats::deferredcompletion::result=readingstats::deferredcompletion::ExecuteMoveResult::Missing;
 rollover.loop();assert(readingstats::deferredcompletion::attempts==before+1 && !rollover.deferredMoveRetryPending);
 HomeActivity cancelled;
 cancelled.deferredMoveRetryPending=true;cancelled.deferredMoveRetryAt=clockMs;
 readingstats::deferredcompletion::show=true;
 cancelled.loop();assert(!cancelled.deferredMoveOverlayShown && cancelled.updates==1);
 readingstats::deferredcompletion::show=false;
#endif
 HomeActivity home;
 // Actual Home activation follows the same reordered entries as the display.
 for (bool libraryFirst : {false, true}) {
   HomeActivity reordered;
   reordered.recentBooks.clear();
   reordered.libraryFirstOnHome=libraryFirst;
   reordered.selectorIndex=0;
   reordered.mappedInput.tapRow=0;
   reordered.loop();
   assert(opened==(libraryFirst ? "library" : "files"));
   reordered.mappedInput.tapRow=1;
   reordered.loop();
   assert(opened==(libraryFirst ? "files" : "library"));
 }
 home.mappedInput.swipe=MappedInputManager::SwipeDir::Up;
 home.loop();assert(home.selectorIndex==6);
 home.mappedInput.swipe=MappedInputManager::SwipeDir::None;
 home.mappedInput.tapRow=0;home.loop();assert(opened=="settings");
 home.mappedInput.tapRow=-1;home.mappedInput.swipe=MappedInputManager::SwipeDir::Down;
 home.loop();assert(home.selectorIndex==1);
 home.mappedInput.swipe=MappedInputManager::SwipeDir::None;
 home.mappedInput.tapRow=2;home.loop();assert(opened=="saved");
 home.mappedInput.tapRow=-1;home.coverGridUi=std::make_unique<CoverGrid>();home.selectorIndex=0;
 home.mappedInput.swipe=MappedInputManager::SwipeDir::Up;home.loop();assert(home.selectorIndex==1);
 home.mappedInput.swipe=MappedInputManager::SwipeDir::Down;home.loop();assert(home.selectorIndex==0);
#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)
 SETTINGS.uiTheme=CrossPointSettings::LYRA_CAROUSEL;
 HomeActivity carousel;
 carousel.recentBooks={{"/one.epub"},{"/two.epub"},{"/three.epub"}};
 carousel.selectorIndex=0;
 carousel.mappedInput.swipe=MappedInputManager::SwipeDir::Left;
 carousel.mappedInput.swipeY=100;
 carousel.mappedInput.released=MappedInputManager::Button::Back;
 opened.clear();carousel.loop();assert(carousel.selectorIndex==1 && opened.empty());
 // A swipe elsewhere must not become a synthetic Back or a menu-page jump.
 carousel.mappedInput.swipeY=740;
 carousel.mappedInput.swipe=MappedInputManager::SwipeDir::Up;
 carousel.loop();assert(carousel.selectorIndex==1 && opened.empty());
 carousel.mappedInput.swipe=MappedInputManager::SwipeDir::None;
 carousel.mappedInput.released=MappedInputManager::Button::Left;
 carousel.mappedInput.tapX=1;carousel.mappedInput.tapY=710;
 carousel.loop();assert(opened=="files");
 carousel.mappedInput.tapX=479;
 carousel.loop();assert(opened=="settings");
 const auto covers=carousellayout::make(0,56,480,300,16);
 carousel.mappedInput.tapX=covers.right.x+1;
 carousel.mappedInput.tapY=covers.right.y+1;
 opened.clear();carousel.loop();assert(carousel.selectorIndex==2 && opened.empty());
 carousel.mappedInput.tapX=covers.center.x+1;
 carousel.mappedInput.tapY=covers.center.y+1;
 carousel.loop();assert(opened=="/three.epub");
 HomeActivity empty;
 empty.recentBooks.clear();empty.selectorIndex=0;
 empty.mappedInput.tapX=479;empty.mappedInput.tapY=710;
 empty.loop();assert(opened=="settings");
#endif
}
'''
with tempfile.TemporaryDirectory() as temporary:
    directory=Path(temporary);(directory/'home.cpp').write_text(code)
    flags=['-DCROSSPOINT_EXPERIMENTAL_CAROUSEL'] if os.environ.get('TEST_CAROUSEL') else []
    if os.environ.get('TEST_DEFERRED_AUTOMATION'):
        flags.append('-DCROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION')
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-I',str(ROOT/'src'),*flags,*shlex.split(os.environ.get('CXXFLAGS','')),
                    str(directory/'home.cpp'),'-o',str(directory/'home')],check=True)
    subprocess.run([str(directory/'home')],check=True)
print('Complete Home loop compiled; classic swipe pages, touch routing and cover-grid swipes passed')
