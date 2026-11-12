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
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "components/themes/lyra/CarouselLayout.h"
struct Rect {int x,y,width,height;};
constexpr int SMALL_FONT_ID=0;
struct Renderer {int width=480,height=800;int getScreenHeight()const{return height;}int getLineHeight(int)const{return 16;}};
struct Metrics {int homeTopPadding=56,homeCoverTileHeight=300,homeMenuTopOffset=16,buttonHintsHeight=40,menuSpacing=8;bool homeContinueReadingInMenu=false;int homeRecentBooksCount=1,contentSidePadding=12;};
struct Theme {bool dashboard=false;bool hasDashboardHome()const{return dashboard;}int getMenuRowHeight(const Renderer&)const{return 64;}int getMenuTopInset()const{return 0;}} GUI;
struct UITheme {static UITheme& getInstance(){static UITheme theme;return theme;}const Metrics& getMetrics()const{static Metrics m;return m;}static Rect getContentArea(const Renderer& r){return {0,0,r.width,r.height};}};
struct MappedInputManager {
 enum class Button {Back,Confirm,Up,Down,Left,Right};
 enum class SwipeDir {None,Up,Down,Left,Right};
 enum class RowTouch {None,Down,Tap};
 SwipeDir swipe=SwipeDir::None;int tapRow=-1;Button released=Button::Left;int swipeY=0,tapX=-1,tapY=-1;
 SwipeDir wasSwipe(int* x=nullptr,int* y=nullptr)const{if(x)*x=0;if(y)*y=swipeY;return swipe;}
 bool wasScreenTapped(int& x,int& y)const{x=tapX;y=tapY;return tapX>=0;}
 Button pressed=Button::Back;
 bool wasPressed(Button b)const{return b==pressed;}
 bool wasReleased(Button b)const{return b==released;}
 RowTouch colTouch(int& index,int x,int width,int count,int top,int bottom,int)const{
  if(count==0||tapX<x||tapX>=x+width*count||tapY<top||tapY>=bottom)return RowTouch::None;
  index=(tapX-x)/width;return RowTouch::Tap;
 }
 RowTouch rowTouch(int& row,int,int,int count,int,int,int)const{if(tapRow<0||tapRow>=count)return RowTouch::None;row=tapRow;return RowTouch::Tap;}
};
struct ButtonNavigator {
 int direction=0;
 static int nextIndex(int index,int count){return (index+1)%count;}
 static int previousIndex(int index,int count){return (index+count-1)%count;}
 template<class F>void onNext(F f){if(direction==1)f();}
 template<class F>void onPrevious(F f){if(direction==-1)f();}
 MappedInputManager::Button pressed=MappedInputManager::Button::Back;
 template<class F>void onPressAndContinuous(std::initializer_list<MappedInputManager::Button> buttons,F f){
  if(std::find(buttons.begin(),buttons.end(),pressed)!=buttons.end())f();}
};
struct CoverGrid {int selectedAction(const MappedInputManager&){return -1;}};
enum class HomeMenuItem {NONE,FILE_BROWSER,LIBRARY,SAVED_ITEMS,OPDS_BROWSER,FILE_TRANSFER,SETTINGS_MENU};
std::string opened;
struct ReadingStatsActivity {ReadingStatsActivity(Renderer&,MappedInputManager&){} };
bool allocationFails=false;
template<class T,class... A>std::unique_ptr<T> makeUniqueNoThrow(A&&... a){if(allocationFails)return {};return std::make_unique<T>(static_cast<A&&>(a)...);}
struct {void replaceActivity(std::unique_ptr<ReadingStatsActivity>){opened="statistics";}void goToSavedItems(){opened="saved";}void goHome(){opened="home";}} activityManager;
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
enum { STR_OK_BUTTON, STR_READING_STATISTICS, STR_READER_STATS_SAVE_FAILED, STR_RETRY, STR_SETTINGS_TITLE, STR_SETTINGS_RECOVERY_DETAILS, STR_FINISHED_MOVE_REVIEW, STR_MENU, STR_BROWSE_FILES, STR_LIBRARY, STR_SAVED_ITEMS, STR_FILE_TRANSFER, STR_PLUGINS, STR_OPDS_BROWSER };
const char* tr(int id) {static std::string labels[30];labels[id]=std::to_string(id);return labels[id].c_str();}
struct Notice { uint32_t pending() const {return 0;} void acknowledge(uint32_t) {} };
Notice& statsSaveNotice(){static Notice notice;return notice;}
Notice& settingsSaveNotice(){static Notice notice;return notice;}
struct CrossPointSettings { enum {MINIMAL_HOME=5,DASHBOARD_HOME=6,LYRA_CAROUSEL=7}; };
struct Settings {int uiTheme=0;bool trackReadingStats=true;bool saveToFile(){return true;}} SETTINGS;
struct Popup {
 bool shown=false,active=false;int count=0;std::vector<std::string> labels;std::function<void(int)> callback;
 void show(const char*,const char*const* options,int n,int,std::function<void(int)> f){active=true;count=n;callback=std::move(f);labels.assign(options,options+n);}
 void choose(int index){assert(index>=0&&index<count);active=false;callback(index);}
 bool isActive()const{return active;}
 template<class F>void showMessage(const char*,const char*,const char**,int,int,F){shown=true;}
 template<class F>bool handleInput(MappedInputManager&,F){return active;}
};
bool renderLocked=false;int errors=0;
#define LOG_ERR(...) (++errors)
struct RenderLock {RenderLock(){assert(!renderLocked);renderLocked=true;}~RenderLock(){renderLocked=false;}};
struct Book {std::string path;bool missing=false;bool operator==(const Book& other)const{return path==other.path;}};
struct RecentBooksStore {static bool isMissing(const Book& b){return b.missing;}};
struct {std::vector<Book> books;const auto& getBooks()const{return books;}} RECENT_BOOKS;
struct MinimalHomeTheme {static Rect coverRect(Rect);};
struct DashboardHomeTheme {static Rect coverRect(Rect);};
namespace MinimalHomeMetrics {struct M {int homeCoverHeight=300;};constexpr M values;}

struct HomeActivity {
 bool libraryFirstOnHome = false;
 bool coverRendered=true,recentsLoaded=true;int frees=0,dataLoads=0;std::string dataPath;
 void freeCoverBuffer(){assert(renderLocked);++frees;}
 void loadDashboardData(){assert(renderLocked);++dataLoads;dataPath=recentBooks.front().path;}
 void activateSelection();void showCompactMenu();void showNextCompactBook();bool handleCompactHome();
 struct MenuPage {int top,first,count,capacity;};
 Renderer renderer;MappedInputManager mappedInput;ButtonNavigator buttonNavigator;Popup popup;
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
 void requestUpdate(){++updates;}
 void onRecentBookSelected(const std::string& path){opened=path;}
 void onFileBrowserOpen(){opened="files";}
 void onLibraryOpen(){opened="library";}
 void onPluginsOpen(){opened="plugins";}
 void onOpdsBrowserOpen(){opened="opds";}
 void onFileTransferOpen(){opened="transfer";}
 void onSettingsOpen(){opened="settings";}
 int getMenuItemCount()const;
 int homeCoverTileHeight()const;
 MenuPage menuPage()const;
 void loop();
'''
code+=method(header,'  static HomeMenuItem indexToMenuItem(')+'\n};\n'
code+=method(source,'bool usesCompactHome(')+'\n'
for file,signature in [('MinimalHomeTheme.cpp','Rect MinimalHomeTheme::coverRect('),('DashboardHomeTheme.cpp','Rect DashboardHomeTheme::coverRect(')]:
    code+=method((ROOT/'src/components/themes/minimal'/file).read_text(),signature)+'\n'
for signature in ['void HomeActivity::activateSelection(', 'void HomeActivity::showCompactMenu(', 'void HomeActivity::showNextCompactBook(', 'bool HomeActivity::handleCompactHome(', 'int HomeActivity::getMenuItemCount(', 'int HomeActivity::homeCoverTileHeight(', 'HomeActivity::MenuPage HomeActivity::menuPage(', 'void HomeActivity::loop(']:
    code+=method(source,signature)+'\n'
code+=r'''
int main(){
#if defined(CROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION)
 HomeActivity warning;
 warning.deferredMoveNeedsReview=true;
 warning.loop();
 assert(warning.popup.shown && !warning.deferredMoveNeedsReview && warning.updates==1);
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
 failed.loop();assert(failed.popup.shown && !failed.deferredMoveRetryPending);
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
 for(int theme:{CrossPointSettings::MINIMAL_HOME,CrossPointSettings::DASHBOARD_HOME}){
  SETTINGS.uiTheme=theme;GUI.dashboard=theme==CrossPointSettings::DASHBOARD_HOME;
  for(bool library:{false,true})for(bool plugins:{false,true})for(bool opds:{false,true})for(bool tracking:{false,true})for(auto release:{MappedInputManager::Button::Back,MappedInputManager::Button::Confirm}){
   SETTINGS.trackReadingStats=tracking;
   HomeActivity minimal;minimal.libraryFirstOnHome=library;minimal.hasPlugins=plugins;minimal.hasOpdsServers=opds;
   minimal.mappedInput.released=release;
   minimal.mappedInput.swipe=MappedInputManager::SwipeDir::Right;
   opened.clear();minimal.loop();assert(opened==(library?"library":"files"));
   minimal.mappedInput.swipe=MappedInputManager::SwipeDir::Down;
   opened.clear();minimal.loop();assert(opened=="settings");
   minimal.mappedInput.swipe=MappedInputManager::SwipeDir::Up;
   opened.clear();minimal.loop();assert(opened.empty()&&minimal.popup.active);
   const int normal=5+(plugins||opds),total=normal+tracking;
   assert(minimal.popup.count==total);
   assert(minimal.popup.labels.front()==tr(library?STR_LIBRARY:STR_BROWSE_FILES));
   // A fresh frame behind an open menu must not resume a book or process another swipe.
   minimal.mappedInput.swipe=MappedInputManager::SwipeDir::Down;
   minimal.loop();assert(opened.empty());
   const std::vector<std::string> expected=plugins||opds?
    std::vector<std::string>{library?"library":"files",library?"files":"library","saved",plugins?"plugins":"opds","transfer","settings"}:
    std::vector<std::string>{library?"library":"files",library?"files":"library","saved","transfer","settings"};
   for(int index=0;index<total;++index){
    minimal.showCompactMenu();opened.clear();minimal.popup.choose(index);
    assert(opened==(index==normal?"statistics":expected[index]));
   }
  }
  SETTINGS.trackReadingStats=true;
  HomeActivity minimal;minimal.selectorIndex=0;minimal.mappedInput.released=MappedInputManager::Button::Left;
  RECENT_BOOKS.books={{"/book.epub"},{"/missing.epub",true},{"/next.epub"}};
  const auto original=RECENT_BOOKS.books;
  minimal.mappedInput.swipe=MappedInputManager::SwipeDir::Left;
  opened.clear();minimal.loop();assert(opened.empty()&&minimal.recentBooks.front().path=="/next.epub");
  assert(minimal.frees==1&&!minimal.coverRendered&&!minimal.recentsLoaded&&!renderLocked);
  assert(minimal.dataLoads==(GUI.dashboard?1:0));
  if(GUI.dashboard)assert(minimal.dataPath=="/next.epub");
  minimal.loop();assert(minimal.recentBooks.front().path=="/book.epub"&&RECENT_BOOKS.books==original&&RECENT_BOOKS.books[1].missing);
  RECENT_BOOKS.books={{"/missing.epub",true}};
  const int frees=minimal.frees;minimal.loop();assert(minimal.frees==frees);
  RECENT_BOOKS.books.clear();minimal.loop();assert(minimal.frees==frees);
  minimal.mappedInput.swipe=MappedInputManager::SwipeDir::None;
  for(int width:{480,800}){
   minimal.renderer.width=width;minimal.renderer.height=width==480?800:480;
   const auto area=Rect{0,56,width,minimal.homeCoverTileHeight()};
   const auto cover=GUI.dashboard?DashboardHomeTheme::coverRect(area):MinimalHomeTheme::coverRect(area);
   minimal.mappedInput.tapX=cover.x;minimal.mappedInput.tapY=cover.y;
   opened.clear();minimal.loop();assert(opened=="/book.epub");
   minimal.mappedInput.tapX=cover.x-1;opened.clear();minimal.loop();assert(opened.empty());
   minimal.mappedInput.tapX=cover.x;minimal.mappedInput.tapY=cover.y+cover.height;
   minimal.loop();assert(opened.empty());
  }
  minimal.mappedInput.tapX=-1;minimal.mappedInput.released=MappedInputManager::Button::Confirm;
  minimal.loop();assert(opened=="/book.epub");
  minimal.recentBooks.clear();opened.clear();minimal.loop();assert(opened.empty());
  allocationFails=true;minimal.showCompactMenu();minimal.popup.choose(minimal.popup.count-1);
  assert(opened.empty()&&errors>0);allocationFails=false;
 }
 SETTINGS.uiTheme=0;GUI.dashboard=false;
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
 // Buttons stay within their row, and Up/Down restore the highlighted cover.
 carousel.mappedInput.tapX=-1;
 carousel.selectorIndex=2;
 carousel.buttonNavigator.pressed=MappedInputManager::Button::Right;
 carousel.loop();assert(carousel.selectorIndex==0 && carousel.carouselCenterIndex==0);
 carousel.buttonNavigator.pressed=MappedInputManager::Button::Left;
 carousel.loop();assert(carousel.selectorIndex==2 && carousel.carouselCenterIndex==2);
 carousel.buttonNavigator.pressed=MappedInputManager::Button::Back;
 carousel.mappedInput.pressed=MappedInputManager::Button::Down;
 carousel.loop();assert(carousel.selectorIndex==3);
 carousel.mappedInput.pressed=MappedInputManager::Button::Back;
 carousel.buttonNavigator.pressed=MappedInputManager::Button::Left;
 carousel.loop();assert(carousel.selectorIndex==8 && carousel.carouselCenterIndex==2);
 carousel.buttonNavigator.pressed=MappedInputManager::Button::Back;
 carousel.mappedInput.pressed=MappedInputManager::Button::Up;
 carousel.loop();assert(carousel.selectorIndex==2);
 carousel.mappedInput.pressed=MappedInputManager::Button::Back;
 carousel.mappedInput.released=MappedInputManager::Button::Confirm;
 opened.clear();carousel.loop();assert(opened=="/three.epub");
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
