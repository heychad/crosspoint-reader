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
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>
struct Rect {int x,y,width,height;};
struct Renderer {int getScreenHeight()const{return 800;}};
struct Metrics {int homeTopPadding=56,homeCoverTileHeight=300,homeMenuTopOffset=16,buttonHintsHeight=40,menuSpacing=8;bool homeContinueReadingInMenu=false;int homeRecentBooksCount=1,contentSidePadding=12;};
struct Theme {int getMenuRowHeight(const Renderer&)const{return 64;}int getMenuTopInset()const{return 0;}} GUI;
struct UITheme {static UITheme& getInstance(){static UITheme theme;return theme;}const Metrics& getMetrics()const{static Metrics m;return m;}static Rect getContentArea(const Renderer&){return {0,0,480,800};}};
struct MappedInputManager {
 enum class Button {Back,Confirm,Up,Down,Left,Right};
 enum class SwipeDir {None,Up,Down,Left,Right};
 enum class RowTouch {None,Down,Tap};
 SwipeDir swipe=SwipeDir::None;int tapRow=-1;Button released=Button::Left;
 SwipeDir wasSwipe()const{return swipe;}
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
enum class HomeMenuItem {NONE,FILE_BROWSER,LIBRARY,SAVED_ITEMS,OPDS_BROWSER,FILE_TRANSFER,SETTINGS_MENU};
std::string opened;
struct {void goToSavedItems(){opened="saved";}} activityManager;
namespace haptic_feedback {void touchAction(){}}
enum { STR_OK_BUTTON, STR_READING_STATISTICS, STR_READER_STATS_SAVE_FAILED };
const char* tr(int) {return "text";}
struct Notice { uint32_t pending() const {return 0;} void acknowledge(uint32_t) {} };
Notice& statsSaveNotice(){static Notice notice;return notice;}
struct Popup {
 bool isActive()const{return false;}
 template<class F>void showMessage(const char*,const char*,const char**,int,int,F){}
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
 MenuPage menuPage()const;
 void loop();
'''
code+=method(header,'  static HomeMenuItem indexToMenuItem(')+'\n};\n'
for signature in ['int HomeActivity::getMenuItemCount(', 'HomeActivity::MenuPage HomeActivity::menuPage(', 'void HomeActivity::loop(']:
    code+=method(source,signature)+'\n'
code+=r'''
int main(){
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
}
'''
with tempfile.TemporaryDirectory() as temporary:
    directory=Path(temporary);(directory/'home.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20',*shlex.split(os.environ.get('CXXFLAGS','')),
                    str(directory/'home.cpp'),'-o',str(directory/'home')],check=True)
    subprocess.run([str(directory/'home')],check=True)
print('Complete Home loop compiled; classic swipe pages, touch routing and cover-grid swipes passed')
