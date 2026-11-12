"""Exercise the production drag state machine with sanitizers and bounded UI stubs."""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/activities/reader/ClipSelectionActivity.cpp").read_text()
header = (ROOT / "src/activities/reader/ClipSelectionActivity.h").read_text()
geometry = (ROOT / "src/clippings/SelectionGeometry.h").read_text().replace(
    '#include "components/themes/BaseTheme.h"', ""
).replace("#pragma once\n", "")


def method(signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if depth == 0:
            return source[start:end + 1]
    raise ValueError(signature)


word_box = "struct WordBox {" + header.split("struct WordBox {", 1)[1].split("  };", 1)[0] + "};"
methods = [
    "int ClipSelectionActivity::closestInRow(",
    "int ClipSelectionActivity::wordAt(",
    "int ClipSelectionActivity::dragWordAt(",
    "bool ClipSelectionActivity::selectionContains(",
    "int ClipSelectionActivity::nextPageStartIndexForTouchDrag(",
    "bool ClipSelectionActivity::isWithinCurrentPageEndDwellSlop(",
    "void ClipSelectionActivity::moveVertical(",
    "void ClipSelectionActivity::selectIndex(",
    "void ClipSelectionActivity::moveToPage(",
    "void ClipSelectionActivity::loop(",
    "bool ClipSelectionActivity::handleButtons(",
    "Rect ClipSelectionActivity::handleRect(",
    "int ClipSelectionActivity::textOffset(",
    "int ClipSelectionActivity::textXOffset(",
    "void ClipSelectionActivity::drawSelection(",
    "void ClipSelectionActivity::ditherGapBetween(",
]
program = r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <climits>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
struct Rect { int x=0,y=0,width=0,height=0; };
''' + geometry + r'''
#define tr(x) "label"
enum class Color { LightGray };
struct EpdFontFamily { enum Style { REGULAR }; };
namespace BidiUtils { enum BidiBaseDir { AUTO }; }
struct ClippingResult { enum class Action { Lookup, Clip, Bookmark }; };
constexpr int TOUCH_DRAG_MOVEMENT_PX=4,TOUCH_PAGE_END_DWELL_SLOP_PX=8;
constexpr unsigned long TOUCH_PAGE_ADVANCE_HOLD_MS=1000;
constexpr ClippingResult::Action SELECTION_ACTIONS[]={ClippingResult::Action::Lookup,ClippingResult::Action::Clip,ClippingResult::Action::Bookmark};
unsigned long now=1; unsigned long millis(){return now;}
int lockDepth=0;
struct RenderLock { RenderLock(){assert(lockDepth==0);++lockDepth;}~RenderLock(){--lockDepth;} };
struct MappedInputManager {
 enum class SwipeDir { None,Left,Right,Up,Down };
 int x=0,y=0;bool tap=false,pressed=false,held=false;uint8_t buttons=0;SwipeDir swipe=SwipeDir::None;
 bool hasTouch()const{return true;}
 bool position(bool active,int&a,int&b)const{a=x;b=y;return active;}
 bool wasScreenTapped(int&a,int&b)const{return position(tap,a,b);}
 bool wasScreenTouchPressed(int&a,int&b)const{return position(pressed,a,b);}
 bool isScreenTouchHeld(int&a,int&b)const{return position(held,a,b);}
 SwipeDir wasSwipe()const{return swipe;}
};
struct Input {
 enum { INPUT_PREVIOUS=1,INPUT_NEXT=2,INPUT_CONFIRM=4,INPUT_BACK=8,INPUT_LEFT=16,INPUT_RIGHT=32,INPUT_UP=64,INPUT_DOWN=128 };
 static uint8_t buttonEdges(const MappedInputManager&i){return i.buttons;}
};
struct Renderer {
 int width=480,height=800;mutable int draws=0;
 int getScreenWidth()const{return width;}
 void fillRectDither(int,int,int,int,Color)const{}
 template<class...T>void drawText(int,int,int,const char*text,T...)const{assert(text&&std::string(text)=="word");++draws;}
 void drawRect(int,int,int,int,bool)const{}
};
struct Theme {
 struct Metrics{int verticalSpacing=12;}metrics;
 static Theme& getInstance(){static Theme t;return t;}
 Rect getScreenSafeArea(const Renderer&r,bool,bool)const{return{0,0,r.width,r.height};}
 const Metrics&getMetrics()const{return metrics;}
 void drawSelectionHandle(const Renderer&,Rect rect,bool)const{assert(rect.width==24&&rect.height==24);}
 void drawSelectionActions(const Renderer&,Rect,int,int)const{}
}GUI;
using UITheme=Theme;
struct Popup {
 bool isActive()const{return false;}
 template<class...T>bool handleInput(T...){return false;}
 template<class...T>bool handleButtons(T...){return false;}
};
struct ClipSelectionActivity {
''' + word_box + r'''
 enum class Mode { Clip,DefinitionLookup }; Mode mode=Mode::Clip;
 Renderer renderer;MappedInputManager mappedInput;Popup actionPopup;std::atomic<bool>renderReady{true};
 std::unique_ptr<WordBox[]>words=std::make_unique<WordBox[]>(240);size_t wordCount=240;
 int selected=0,rangeStart=-1,currentPageOffset=0,rowCount=30,lineHeight=18;
 bool touchDragSelecting=false,ignoreInitialTouch=false,touchDragHasMoved=false,cancelled=false;
 int dragOffsetX=0,dragOffsetY=0,touchDragStartX=0,touchDragStartY=0,touchDragPageEndIndex=-1;
 unsigned long touchDragPageEndHeldSince=0;int updates=0,confirmations=0;
 size_t pageCount()const{return 3;}
 bool isLookup()const{return false;}
 int firstAction()const{return 1;}int actionCount()const{return 2;}
 int actionAt(int,int)const{return -1;}Rect actionRect()const{return{};}
 void cancel(){cancelled=true;}
 void confirmSelection(ClippingResult::Action){++confirmations;}
 void requestUpdate(){++updates;}
 void loopButtons(){assert(false);}
 int closestInRow(uint16_t,int)const;int wordAt(int,int)const;int dragWordAt(int,int)const;
 bool selectionContains(int,int)const;int nextPageStartIndexForTouchDrag()const;
 bool isWithinCurrentPageEndDwellSlop(int,int)const;
 void moveVertical(int);void selectIndex(int);void moveToPage(int);void loop();bool handleButtons(uint8_t);
 Rect handleRect(int,bool)const;int textOffset()const;int textXOffset()const;void drawSelection()const;
 void ditherGapBetween(const WordBox&,const WordBox&,int,int)const;
 void populate(int width,int height){
  renderer.width=width;renderer.height=height;
  for(int i=0;i<240;++i){auto&w=words[i];int onPage=i%80;
   w.x=12+(onPage%8)*(width-24)/8;w.y=30+(onPage/8)*(height-80)/10;
   w.width=(width-24)/8-5;w.height=18;w.row=i/8;w.pageOffset=i/80;w.text="word";
  }
 }
 void check()const{
  assert(selected>=0&&selected<int(wordCount));assert(rangeStart>=-1&&rangeStart<int(wordCount));
  assert(currentPageOffset<int(pageCount()));assert(words[selected].pageOffset==currentPageOffset);
  drawSelection();assert(lockDepth==0);
 }
 void frame(int x,int y,bool held=false,bool press=false,bool tap=false,uint8_t buttons=0,
            MappedInputManager::SwipeDir swipe=MappedInputManager::SwipeDir::None){
  mappedInput={x,y,tap,press,held,buttons,swipe};loop();check();++now;
 }
};
''' + "\n".join(method(name) for name in methods) + r'''
int main(){
 for(auto dimensions: {std::pair{480,800},std::pair{800,480}}){
  ClipSelectionActivity c;c.populate(dimensions.first,dimensions.second);
  // The initial held long press must be released before a new drag begins.
  c.ignoreInitialTouch=true;c.rangeStart=0;
  c.frame(20,35,true);assert(!c.touchDragSelecting&&c.ignoreInitialTouch);
  c.frame(20,35);assert(!c.ignoreInitialTouch);
  c.rangeStart=-1;c.frame(20,35,true,true);assert(c.touchDragSelecting&&c.rangeStart==0);
  // Dwell at the bottom advances at most one page at a time, up to the 240-word cap.
  for(int page=0;page<2;++page){
   c.frame(c.renderer.width-1,c.renderer.height-1,true);
   now+=TOUCH_PAGE_ADVANCE_HOLD_MS;
   c.frame(c.renderer.width-1,c.renderer.height-1,true);
   assert(c.currentPageOffset==page+1);
  }
  for(int n=0;n<4;++n){now+=TOUCH_PAGE_ADVANCE_HOLD_MS;c.frame(c.renderer.width+80,c.renderer.height+80,true);}
  assert(c.selected==239&&c.rangeStart==0);c.frame(0,0);
  assert(!c.touchDragSelecting&&!c.cancelled&&c.confirmations==0);
  // Both handles can cross their opposite endpoint without producing an invalid index.
  c.currentPageOffset=0;c.rangeStart=8;c.selected=24;
  for(bool start: {true,false}){
   int index=start?std::min(c.rangeStart,c.selected):std::max(c.rangeStart,c.selected);
   Rect handle=c.handleRect(index,start);
   c.frame(handle.x+handle.width/2,handle.y+handle.height/2,true,true);
   assert(c.touchDragSelecting);
   int anchor=c.rangeStart;
   c.frame(c.renderer.width+100,15,true);assert(c.selected<anchor);
   c.frame(-100,c.renderer.height-30,true);assert(c.selected>anchor);c.frame(0,0);
  }
  // A held finger owns the frame; Back clears the range and then cancels after release.
  c.rangeStart=0;c.selected=20;c.currentPageOffset=0;
  Rect handle=c.handleRect(20,false);c.frame(handle.x+12,handle.y+12,true,true);
  c.frame(0,0,true,false,false,Input::INPUT_BACK);assert(c.touchDragSelecting&&c.rangeStart==0);
  c.frame(0,0,false,false,false,Input::INPUT_BACK);
  c.frame(0,0,false,false,false,Input::INPUT_BACK);assert(c.rangeStart==-1&&!c.cancelled);
  c.frame(0,0,false,false,false,Input::INPUT_BACK);assert(c.cancelled&&c.confirmations==0);
  // Thousands of held/pressed/released frames exercise coordinates outside the view too.
  ClipSelectionActivity stress;stress.populate(dimensions.first,dimensions.second);
  uint32_t seed=0x12345678;
  for(int n=0;n<4000;++n){
   seed=seed*1664525U+1013904223U;int x=int(seed%uint32_t(stress.renderer.width+200))-100;
   seed=seed*1664525U+1013904223U;int y=int(seed%uint32_t(stress.renderer.height+200))-100;
   stress.frame(x,y,n%5!=0,n%13==0);now+=3;
  }
 }
}
'''

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    cpp = path / "selection.cpp"
    executable = path / "selection"
    cpp.write_text(program)
    subprocess.run([
        "c++", "-std=c++20", "-g", "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer", str(cpp), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)

print("Production touch selection passes handle, dwell, cap, cancellation, and coordinate stress checks")
