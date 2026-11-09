"""Exercise production gesture dispatch with contended rendering and real controller state."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'src/activities/reader/ReaderGestureInput.cpp').read_text()
result_method='bool ReaderActivity::applyReaderGestureResult(' + source.split('bool ReaderActivity::applyReaderGestureResult(',1)[1].split('#endif',1)[0]
input_method='bool ReaderActivity::handleReaderGestureInput(' + source.split('bool ReaderActivity::handleReaderGestureInput(',1)[1].split('void ReaderActivity::finishReaderGestures()',1)[0]
code=r'''
#define FREEINK_CAP_TOUCH 1
#include <atomic>
#include <cassert>
#include "personalization/ReaderGestures.h"
using readershortcuts::Action;
bool renderAvailable=true,renderHeld=false;
struct RenderLock {
  enum class Mode{Blocking,Try};
  bool held=false;
  explicit RenderLock(Mode mode) { assert(mode==Mode::Try);held=renderAvailable;if(held)renderHeld=true; }
  ~RenderLock(){if(held)renderHeld=false;}
  bool ownsLock()const{return held;}
};
struct GfxRenderer {
  int getScreenWidth()const{return 480;} int getScreenHeight()const{return 800;}
  int getOrientation()const{return 0;}
};
struct MappedInputManager {
  struct Point{int x=10,y=400;uint8_t id=1;bool valid=true;};
  struct TouchSnapshot{std::array<Point,4>contacts{};uint8_t count=0,reportedCount=0;bool idsStable=true;} sample;
  struct CompletedSwipe{int startX=0,startY=0,endX=0,endY=0;unsigned long durationMs=0;uint8_t contactCount=0;};
  struct CompletedTransform{float value=0;};
  int suppressions=0,reads=0;
  bool hasTouch()const{return true;}
  bool touchContactsDown()const{return sample.count!=0;}
  bool supportsMultiTouch()const{return true;}
  TouchSnapshot getTouchSnapshot(){assert(renderHeld);++reads;return sample;}
  bool wasCompletedMultiTouchSwipe(CompletedSwipe&)const{return false;}
  bool wasCompletedMultiTouchRotation(CompletedTransform&)const{return false;}
  bool wasCompletedMultiTouchPinch(CompletedTransform&)const{return false;}
  bool completedSingleSwipe(int&,int&,int&,int&)const{return false;}
  void suppressTouchContact(){++suppressions;}
};
struct Settings{
  uint8_t touchReaderControls=1,pinchFontResizeEnabled=1,twoFingerRotationEnabled=1;
  std::array<uint8_t,4>twoFingerActions{};
  uint8_t readerLeftEdgeUp=0,readerLeftEdgeDown=0,readerRightEdgeUp=0,readerRightEdgeDown=0;
}SETTINGS;
unsigned long millis(){return 100;}
class ReaderActivity{
 public:
  readergestures::Controller gestureController;
  readergestures::Frame gestureFrame;
  GfxRenderer renderer;MappedInputManager mappedInput;std::atomic<bool>pageRendered{true};
  bool overlay=false,end=false;int started=0,actions=0,begins=0,cancels=0,finishes=0;
  bool inputOverPage()const{return overlay;} bool isAtEndOfBook()const{return end;}
  void onReaderGestureStarted(){assert(renderHeld);++started;}
  void handleReaderShortcut(Action){++actions;}
  void applyLightGesture(const readergestures::Event& event){
    using E=readergestures::EventKind;
    if(event.kind==E::LightBegin)++begins;if(event.kind==E::LightCancel)++cancels;if(event.kind==E::LightFinish)++finishes;
  }
  bool applyReaderGestureResult(const readergestures::Result&);
  bool handleReaderGestureInput();
  void sample(uint8_t count,int y=400){mappedInput.sample.count=mappedInput.sample.reportedCount=count;mappedInput.sample.contacts[0].y=y;}
};
''' + result_method + input_method + r'''
int main(){
  renderAvailable=false;
  ReaderActivity inherited;inherited.sample(1);
  assert(inherited.handleReaderGestureInput());
  assert(inherited.mappedInput.suppressions==1);
  renderAvailable=true;assert(inherited.handleReaderGestureInput());
  inherited.sample(0);assert(inherited.handleReaderGestureInput());
  assert(!inherited.handleReaderGestureInput());assert(inherited.actions==0 && inherited.started==0);
  ReaderActivity reader;reader.handleReaderGestureInput();reader.overlay=true;
  int routedReleases=0;
  for(uint8_t count:{1,1,0}){reader.sample(count);bool consumed=reader.handleReaderGestureInput();assert(!consumed);if(!count)++routedReleases;}
  assert(routedReleases==1 && reader.mappedInput.suppressions==0);
  reader.overlay=false;reader.sample(0);reader.handleReaderGestureInput();
  reader.overlay=true;reader.sample(1);assert(!reader.handleReaderGestureInput());
  reader.overlay=false;assert(reader.handleReaderGestureInput());reader.sample(0);assert(reader.handleReaderGestureInput());
  assert(!reader.handleReaderGestureInput());
  renderAvailable=false;const int reads=reader.mappedInput.reads;
  for(int i=0;i<50;++i)assert(!reader.handleReaderGestureInput());
  assert(reader.mappedInput.reads==reads);
  reader.sample(1);assert(!reader.handleReaderGestureInput());
  renderAvailable=true;assert(!reader.handleReaderGestureInput());reader.sample(0);assert(!reader.handleReaderGestureInput());
  SETTINGS.readerLeftEdgeUp=static_cast<uint8_t>(Action::Brighter);
  reader.sample(1);reader.handleReaderGestureInput();reader.sample(1,200);assert(reader.handleReaderGestureInput());
  assert(reader.begins==1 && reader.started==1);
  renderAvailable=false;assert(reader.handleReaderGestureInput());assert(reader.cancels==1 && reader.finishes==0);
  reader.sample(0);assert(reader.handleReaderGestureInput());renderAvailable=true;assert(!reader.handleReaderGestureInput());
  SETTINGS.touchReaderControls=0;reader.sample(1);assert(!reader.handleReaderGestureInput());reader.sample(0);assert(!reader.handleReaderGestureInput());
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory);(p/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20','-I',str(root/'src'),str(p/'test.cpp'),str(root/'src/personalization/ReaderGestures.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('Reader overlay, lock contention and gesture routing checks passed')
