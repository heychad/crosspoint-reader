"""Run production touch-page detection with gestures disabled and restored."""
from pathlib import Path
import subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'src/activities/reader/ReaderUtils.h').read_text()
def function(signature):
    start=s.index(signature);end=s.index('{',start)+1;depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]
code=r'''
#include <cassert>
#include <cstdint>
struct CrossPointSettings {
 enum{TAP_AND_SWIPE,TAP_ONLY,SWIPE_ONLY,INVERTED_TAP,PAGE_TURN_GESTURE_DISABLED};
 enum{READER_MENU_OFF,READER_MENU_TAP,READER_MENU_SWIPE_UP};
};
struct Settings:CrossPointSettings{bool touchReaderControls=true,disablePageTurnGestures=false;uint8_t pageTurnGesture=TAP_AND_SWIPE,previousPageGesture=SWIPE_ONLY,showReaderMenu=READER_MENU_TAP;}SETTINGS;
struct GfxRenderer{int getScreenWidth()const{return 480;}int getScreenHeight()const{return 800;}};
struct MappedInputManager{
 enum class SwipeDir{None,Left,Right};SwipeDir swipe=SwipeDir::None;
 bool tapped=false,menu=false,bottom=false;int x=400,y=400;
 bool hasTouch()const{return true;} SwipeDir wasSwipe()const{return swipe;}
 bool wasScreenTapped(int& a,int& b)const{a=x;b=y;return tapped;}
 bool wasMenuGesture()const{return menu;}bool wasReaderMenuSwipeUp()const{return bottom;}
};
struct Gpio{unsigned long lastTouchHeldMs(){return 7;}}gpio;
struct TouchPageTurn{bool prev,next;unsigned long heldMs;};
''' + '\n'.join(function(sig) for sig in ['inline bool gestureAllowsSwipe(', 'inline bool gestureAllowsTap(', 'inline TouchPageTurn detectTouchPageTurn(', 'inline bool isTouchMenuTap(', 'inline bool isTouchMenuGesture(']) + r'''
int main(){
 GfxRenderer r;MappedInputManager input;input.tapped=true;
 assert(detectTouchPageTurn(r,input).next);
 SETTINGS.disablePageTurnGestures=true;
 assert(!detectTouchPageTurn(r,input).next);
 input.swipe=MappedInputManager::SwipeDir::Left;assert(!detectTouchPageTurn(r,input).next);
 input.swipe=MappedInputManager::SwipeDir::Right;assert(!detectTouchPageTurn(r,input).prev);
 input.swipe=MappedInputManager::SwipeDir::None;input.x=240;
 assert(isTouchMenuGesture(r,input));
 input.tapped=false;input.menu=true;assert(isTouchMenuGesture(r,input));
 input.menu=false;input.bottom=true;SETTINGS.showReaderMenu=CrossPointSettings::READER_MENU_SWIPE_UP;
 assert(isTouchMenuGesture(r,input));
 SETTINGS.disablePageTurnGestures=false;input.bottom=false;input.swipe=MappedInputManager::SwipeDir::Right;
 assert(detectTouchPageTurn(r,input).prev);
 assert(SETTINGS.pageTurnGesture==CrossPointSettings::TAP_AND_SWIPE && SETTINGS.previousPageGesture==CrossPointSettings::SWIPE_ONLY);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'t.cpp').write_text(code)
 subprocess.run(['c++','-std=c++20',str(p/'t.cpp'),'-o',str(p/'t')],check=True)
 subprocess.run([str(p/'t')],check=True)
print('Page gestures stop and restore without disabling menu touch paths or changing configured modes')
