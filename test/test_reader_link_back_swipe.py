"""Exercise page swipes against EPUB link-return handling and real edge geometry."""

from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
mapped = (ROOT / "src/MappedInputManager.cpp").read_text()
utils = (ROOT / "src/activities/reader/ReaderUtils.h").read_text()
reader = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text()


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


start = reader.index("  if (footnoteDepth > 0 &&", reader.index("  // Link taps take priority"))
end = reader.index("  if (SETTINGS.shortPwrBtn", start)
link_return = reader[start:end]

code = r'''
#define FREEINK_CAP_TOUCH 1
#include <FreeInkUICore.h>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <initializer_list>
namespace fui = freeink::ui;
struct CrossPointSettings {
 enum { TAP_AND_SWIPE, TAP_ONLY, SWIPE_ONLY, INVERTED_TAP, PAGE_TURN_GESTURE_DISABLED };
 enum { READER_MENU_OFF, READER_MENU_TAP, READER_MENU_SWIPE_UP };
};
struct Settings : CrossPointSettings {
 bool touchReaderControls=true,disablePageTurnGestures=false,backShortToFileBrowser=false;
 uint8_t pageTurnGesture=TAP_AND_SWIPE,previousPageGesture=TAP_AND_SWIPE,showReaderMenu=READER_MENU_TAP;
 int backHoldAction=0;
} SETTINGS;
struct HalGPIO {
 bool swipe=false,tap=false,back=false;float sx=0,sy=0,ex=0,ey=0;
 bool wasSwipe(float& a,float& b,float& c,float& d) const {a=sx;b=sy;c=ex;d=ey;return swipe;}
 bool wasTouchTap(float&,float&) const {return false;}
 bool wasReleased(uint8_t) const {return back;}
 unsigned long lastTouchHeldMs()const{return 7;}
} gpio;
struct GfxRenderer {
 int orientation=0;
 int getScreenWidth()const{return orientation%2?800:480;}
 int getScreenHeight()const{return orientation%2?480:800;}
 void tapToLogical(float nx,float ny,int& x,int& y)const{
  const int px=std::lround(nx*480),py=std::lround(ny*800);
  switch(orientation){case 0:x=px;y=py;break;case 1:x=800-py;y=px;break;
   case 2:x=480-px;y=800-py;break;case 3:x=py;y=480-px;break;}
 }
 void physical(int x,int y,float& nx,float& ny)const{
  switch(orientation){case 0:nx=x/480.f;ny=y/800.f;break;case 1:nx=y/480.f;ny=(800-x)/800.f;break;
   case 2:nx=(480-x)/480.f;ny=(800-y)/800.f;break;case 3:nx=(480-y)/480.f;ny=x/800.f;break;}
 }
};
struct HeaderBackTapTarget{static bool contains(int,int){return false;}};
enum class RemoteButton{None,Confirm,Back,Next,Previous};
enum class HomeButtonAction{Ignore,Confirm};
struct MappedInputManager {
 enum class Button{Back,Confirm,NavNext,Right,NavPrevious,Left};
 enum class SwipeDir{None,Left,Right,Up,Down};
 HalGPIO& gpio;GfxRenderer& renderer;unsigned long held=7;bool longBack=false;
 struct Remote{RemoteButton button=RemoteButton::None;}remoteInput;
 HomeButtonAction homeAction=HomeButtonAction::Ignore;
 bool decodeSwipe(int&,int&,int&,int&)const;
 SwipeDir wasSwipe(int* startX=nullptr,int* startY=nullptr)const;
 bool wasEdgeSwipe(fui::ScreenEdge)const;bool wasBackGesture()const;bool wasReleased(Button)const;
 void rememberTouchHeldTime()const{}
 bool hasTouch()const{return true;}
 bool wasScreenTapped(int& x,int& y)const{x=10;y=400;return gpio.tap;}
 bool wasPowerConfirmClick()const{return false;}
 bool mapButton(Button b,bool(HalGPIO::*fn)(uint8_t)const)const{return b==Button::Back&&(gpio.*fn)(0);}
 bool wasLongPressed(Button,unsigned long)const{return longBack;}
 unsigned long getHeldTime()const{return held;}
};
''' + "\n".join(function(mapped, signature) for signature in [
    "bool MappedInputManager::decodeSwipe(",
    "MappedInputManager::SwipeDir MappedInputManager::wasSwipe(",
    "bool MappedInputManager::wasEdgeSwipe(",
    "bool MappedInputManager::wasBackGesture(",
    "bool MappedInputManager::wasReleased(",
]) + r'''
struct ActivityManager{int files=0;void goToFileBrowser(const char*){++files;}};
namespace ReaderUtils{
constexpr unsigned long GO_BACK_OR_HOME_MS=1000;
struct TouchPageTurn{bool prev,next;unsigned long heldMs;};
struct BackNavCallback{void*ctx;void(*fn)(void*);};
''' + "\n".join(function(utils, signature) for signature in [
    "inline bool gestureAllowsSwipe(", "inline bool gestureAllowsTap(",
    "inline TouchPageTurn detectTouchPageTurn(", "inline bool handleBackNavigation(",
]) + r'''
}
struct Reader {
 GfxRenderer renderer;MappedInputManager mappedInput{gpio,renderer};ActivityManager manager;
 int footnoteDepth=2,restores=0,homes=0,previous=0,next=0;
 void restoreSavedPosition(){--footnoteDepth;++restores;}
 bool handleBackNavigation(){return ReaderUtils::handleBackNavigation(mappedInput,manager,"/book.epub",
  {this,[](void* p){++static_cast<Reader*>(p)->homes;}});}
 void loop(bool rtl=false){
  const auto touch=ReaderUtils::detectTouchPageTurn(renderer,mappedInput,rtl);
''' + link_return + r'''
  previous+=touch.prev;next+=touch.next;
 }
 void swipe(bool right,bool edge=true){
  gpio={};gpio.swipe=true;const int w=renderer.getScreenWidth(),h=renderer.getScreenHeight();
  renderer.physical(right?(edge?10:w/3):w-10,h/2,gpio.sx,gpio.sy);
  renderer.physical(right?w-10:10,h/2,gpio.ex,gpio.ey);
 }
};
int main(){
 for(int orientation=0;orientation<4;++orientation)for(bool rtl:{false,true}){
  for(bool edge:{false,true}){
   SETTINGS={};Reader r;r.renderer.orientation=orientation;r.swipe(true,edge);
   assert(r.mappedInput.wasBackGesture()==edge);
   assert(r.mappedInput.wasReleased(MappedInputManager::Button::Back)==edge);
   r.loop(rtl);assert(r.restores==0 && r.footnoteDepth==2);
   assert(r.previous==!rtl && r.next==rtl && r.homes==0 && r.manager.files==0);
  }
  SETTINGS={};Reader left;left.renderer.orientation=orientation;left.swipe(false);left.loop(rtl);
  assert(left.restores==0 && left.previous==rtl && left.next==!rtl);
  for(bool globalDisable:{false,true}){
   SETTINGS={};SETTINGS.disablePageTurnGestures=globalDisable;SETTINGS.touchReaderControls=globalDisable;
   Reader disabled;disabled.renderer.orientation=orientation;disabled.swipe(true);disabled.loop();
   assert(disabled.restores==0 && disabled.previous==0 && disabled.next==0);
  }
  for(auto mode:{CrossPointSettings::TAP_ONLY,CrossPointSettings::PAGE_TURN_GESTURE_DISABLED}){
   SETTINGS={};SETTINGS.pageTurnGesture=SETTINGS.previousPageGesture=mode;
   Reader disabled;disabled.renderer.orientation=orientation;disabled.swipe(true);disabled.loop(rtl);
   assert(disabled.restores==0 && disabled.previous==0 && disabled.next==0);
  }
 }
 SETTINGS={};gpio={};gpio.tap=true;Reader tap;tap.loop();assert(tap.previous==1 && tap.restores==0);
 for(bool remote:{false,true}){
  SETTINGS={};gpio={};Reader back;gpio.back=!remote;
  if(remote)back.mappedInput.remoteInput.button=RemoteButton::Back;
  back.loop();assert(back.restores==1 && back.footnoteDepth==1 && back.previous==0 && back.homes==0);
 }
 SETTINGS={};gpio={};Reader held;gpio.back=true;held.mappedInput.held=1200;held.mappedInput.longBack=true;
 held.loop();assert(held.restores==0 && held.manager.files==1);
 SETTINGS={};Reader menu;menu.swipe(true);assert(menu.mappedInput.wasReleased(MappedInputManager::Button::Back));
 Reader noLink;noLink.footnoteDepth=0;noLink.swipe(true);noLink.loop();assert(noLink.previous==1);
}
'''

with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p / "test.cpp").write_text(code)
    flags = os.environ.get("CXXFLAGS", "").split()
    subprocess.run(["c++", "-std=c++20", *flags, "-I", str(ROOT / "freeink-sdk/libs/ui/FreeInkUI/include"),
                    str(p / "test.cpp"), "-o", str(p / "test")], check=True)
    subprocess.run([str(p / "test")], check=True)
print("Reader swipes preserve link history; explicit Back and menu Back remain available")
