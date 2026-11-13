"""Exercise production reader-bar tap routing with logical touch coordinates."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def method(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise ValueError(signature)

code = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include "activities/reader/ReaderStatusBarTapTarget.h"
#include "personalization/ReaderBars.h"
struct CrossPointSettings { enum { XTC_STATUS_BAR_HIDE, XTC_STATUS_BAR_BOTTOM, XTC_STATUS_BAR_TOP,XTC_STATUS_BAR_BOTH }; };
struct Settings {
 bool readerBarsEnabled=false,legacyXtcTopUsesBottom=false;ReaderBar readerTopBar,readerBottomBar;
 int tapToHideStatusBar=1,touchReaderControls=1,mode=0;
 struct Spec {int xtcMode;};Spec statusBarSpec()const{return {mode};}
} SETTINGS;
struct Renderer {
 int height=800,top=5,right=0,bottom=7,left=0;
 int getScreenHeight()const{return height;}
 void getOrientedViewableTRBL(int* t,int* r,int* b,int* l)const{*t=top;*r=right;*b=bottom;*l=left;}
};
struct MappedInputManager {
 enum class Button {Confirm};
 bool hardware=true,tap=false,confirm=false,menu=false;int x=100,y=0,reads=0;
 bool hasTouch()const{return hardware;}
 bool wasScreenTapped(int& tx,int& ty){++reads;tx=x;ty=y;return tap;}
 bool wasReleased(Button)const{return confirm;}
};
struct ReaderActivity {
 Renderer renderer;MappedInputManager mappedInput;
 std::atomic<bool> statusBarsVisible{true};int paints=0;
 void requestUpdate(){++paints;}
 bool handleStatusBarTap(int,int,int=0);
};
namespace readerbars {int height(const Renderer&,const ReaderBar& bar){return bar.hidden?0:24;}}
struct UITheme {static UITheme& getInstance(){static UITheme instance;return instance;}int getStatusBarHeight()const{return 24;}};
namespace ReaderUtils {bool isTouchMenuGesture(const Renderer&,const MappedInputManager& input){return input.menu;}}
namespace haptic_feedback {int taps=0;void touchAction(){++taps;}}
struct XtcReaderActivity : ReaderActivity {
 bool xtc=true;int menus=0;
 void openReaderMenu(){++menus;}
 bool handleFormatInput();
};
// INSERT_METHODS
int main(){
 // Logical dimensions and viewable margins after each of the four rotations.
 const Renderer rotations[]={{800,5,3,7,2},{800,7,2,5,3},{480,3,7,2,5},{480,2,5,3,7}};
 for(const auto& renderer:rotations){
  ReaderActivity reader;reader.renderer=renderer;auto& input=reader.mappedInput;
  input.tap=true;input.y=0;
  assert(reader.handleStatusBarTap(24,8,-5) && !reader.statusBarsVisible && reader.paints==1);
  assert(reader.handleStatusBarTap(24,8,-5) && reader.statusBarsVisible && reader.paints==2);
  input.y=renderer.height-1;
  assert(reader.handleStatusBarTap(24,8,-5) && !reader.statusBarsVisible);
  input.y=renderer.height/2;
  assert(!reader.handleStatusBarTap(24,8,-5) && !reader.statusBarsVisible);
  input.y=0;
  assert(!reader.handleStatusBarTap(0,8) && !reader.statusBarsVisible);
  input.y=renderer.height-1;
  assert(!reader.handleStatusBarTap(24,0) && !reader.statusBarsVisible);
  input.tap=false;
  assert(!reader.handleStatusBarTap(24,8) && !reader.statusBarsVisible);
  input.tap=true;input.hardware=false;
  const int reads=input.reads;
  assert(!reader.handleStatusBarTap(24,8) && input.reads==reads);
  input.hardware=true;SETTINGS.touchReaderControls=0;
  assert(!reader.handleStatusBarTap(24,8) && input.reads==reads);
  SETTINGS.touchReaderControls=1;SETTINGS.tapToHideStatusBar=0;
  assert(!reader.handleStatusBarTap(24,8) && reader.statusBarsVisible && input.reads==reads);
  SETTINGS.tapToHideStatusBar=1;
  const int top=std::max(0,renderer.top-5);
  assert(ReaderStatusBarTapTarget::containsTop(top+63,renderer.height,top,24));
  assert(!ReaderStatusBarTapTarget::containsTop(top+64,renderer.height,top,24));
  const int bottom=renderer.height-renderer.bottom;
  assert(ReaderStatusBarTapTarget::containsBottom(bottom-64,renderer.height,renderer.bottom,8));
  assert(!ReaderStatusBarTapTarget::containsBottom(bottom-65,renderer.height,renderer.bottom,8));
 }
 for(int height:{-1,0,480,800}) for(int margin:{-1,0,7,1000}) for(int size:{0,8,24,2000}){
  assert(!ReaderStatusBarTapTarget::containsTop(-1,height,margin,size));
  assert(!ReaderStatusBarTapTarget::containsBottom(height,height,margin,size));
  if(height<=0 || margin<0 || size<=0){
   assert(!ReaderStatusBarTapTarget::containsTop(0,height,margin,size));
   assert(!ReaderStatusBarTapTarget::containsBottom(0,height,margin,size));
  }
 }
 for(int mode:{CrossPointSettings::XTC_STATUS_BAR_HIDE,CrossPointSettings::XTC_STATUS_BAR_BOTTOM,CrossPointSettings::XTC_STATUS_BAR_TOP,CrossPointSettings::XTC_STATUS_BAR_BOTH}){
  SETTINGS.mode=mode;XtcReaderActivity reader;reader.mappedInput.tap=true;
  reader.mappedInput.y=0;
  assert(reader.handleFormatInput()==(mode==CrossPointSettings::XTC_STATUS_BAR_TOP || mode==CrossPointSettings::XTC_STATUS_BAR_BOTH));
  assert(reader.statusBarsVisible==(mode!=CrossPointSettings::XTC_STATUS_BAR_TOP && mode!=CrossPointSettings::XTC_STATUS_BAR_BOTH));
  reader.statusBarsVisible=true;reader.mappedInput.y=799;
  assert(reader.handleFormatInput()==(mode==CrossPointSettings::XTC_STATUS_BAR_BOTTOM || mode==CrossPointSettings::XTC_STATUS_BAR_BOTH));
  assert(reader.statusBarsVisible==(mode!=CrossPointSettings::XTC_STATUS_BAR_BOTTOM && mode!=CrossPointSettings::XTC_STATUS_BAR_BOTH));
  reader.mappedInput.tap=false;reader.mappedInput.menu=true;
  assert(reader.handleFormatInput() && reader.menus==1);
  reader.mappedInput.menu=false;reader.mappedInput.confirm=true;
  assert(reader.handleFormatInput() && reader.menus==2);
  reader.xtc=false;reader.mappedInput.tap=true;reader.mappedInput.y=0;
  assert(!reader.handleFormatInput() && reader.menus==2);
 }
}
'''
methods = [method('src/activities/reader/ReaderActivity.cpp', 'bool ReaderActivity::handleStatusBarTap('),
           method('src/activities/reader/XtcReaderActivity.cpp', 'bool XtcReaderActivity::handleFormatInput(')]
code = code.replace('// INSERT_METHODS', '\n'.join(methods))
with tempfile.TemporaryDirectory() as directory:
    temporary = Path(directory)
    (temporary / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', *shlex.split(os.environ.get('CXXFLAGS', '')),
                    '-I', str(ROOT / 'src'), str(temporary / 'test.cpp'), '-o', str(temporary / 'test')], check=True)
    subprocess.run([str(temporary / 'test')], check=True)
print('Reader bar taps, restoration, rotated margins, disabled touch, and native XTC input routing passed')
