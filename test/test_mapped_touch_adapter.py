"""Check the firmware adapter forwards all contacts/events through logical coordinates."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'src/MappedInputManager.cpp').read_text()
methods = 'bool MappedInputManager::supportsMultiTouch()' + source.split('bool MappedInputManager::supportsMultiTouch()',1)[1].split('void MappedInputManager::rememberTouchHeldTime()',1)[0]
code = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
struct HalGPIO {
  struct Point { float x=0,y=0; uint8_t id=0; bool valid=false; };
  struct Snapshot { std::array<Point,4> contacts{}; uint8_t count=0,reportedCount=0; bool idsStable=false; } sample;
  bool present=true,event=true;
  bool supportsMultiTouch() const { return present; }
  Snapshot getTouchSnapshot() const { return sample; }
  bool wasMultiTouchSwipe(uint8_t& n,float& sx,float& sy,float& ex,float& ey,unsigned long& ms) const {
    n=2;sx=.25f;sy=.5f;ex=.75f;ey=.875f;ms=123;return event;
  }
  bool wasMultiTouchRotation(float& value,float& x,float& y,unsigned long& ms) const {
    value=-33;x=.25f;y=.5f;ms=234;return event;
  }
  bool wasMultiTouchPinch(float& value,float& x,float& y,unsigned long& ms) const {
    value=1.3f;x=.25f;y=.5f;ms=345;return event;
  }
};
struct GfxRenderer {
  int orientation=0;
  void tapToLogical(float nx,float ny,int& x,int& y) const {
    const int px=std::lround(nx*400),py=std::lround(ny*800);
    switch(orientation) {
      case 0:x=px;y=py;break; case 1:x=800-py;y=px;break;
      case 2:x=400-px;y=800-py;break; case 3:x=py;y=400-px;break;
    }
  }
};
class MappedInputManager {
 public:
  struct TouchContact { int x=0,y=0;uint8_t id=0;bool valid=false; };
  struct TouchSnapshot {std::array<TouchContact,4> contacts{};uint8_t count=0,reportedCount=0;bool idsStable=false;};
  struct CompletedSwipe {int startX=0,startY=0,endX=0,endY=0;unsigned long durationMs=0;uint8_t contactCount=0;};
  struct CompletedTransform {float value=0;int centerX=0,centerY=0;unsigned long durationMs=0;};
  HalGPIO& gpio;GfxRenderer& renderer;
  bool supportsMultiTouch() const;
  TouchSnapshot getTouchSnapshot() const;
  bool wasCompletedMultiTouchSwipe(CompletedSwipe&) const;
  bool wasCompletedMultiTouchRotation(CompletedTransform&) const;
  bool wasCompletedMultiTouchPinch(CompletedTransform&) const;
};
''' + methods + r'''
int main() {
  HalGPIO gpio;GfxRenderer renderer;MappedInputManager input{gpio,renderer};
  gpio.sample.count=4;gpio.sample.reportedCount=5;gpio.sample.idsStable=true;
  for(unsigned i=0;i<4;++i)gpio.sample.contacts[i]={.25f,.5f,uint8_t(10+i),true};
  for(int orientation=0;orientation<4;++orientation) {
    renderer.orientation=orientation;int x,y,ex,ey;
    renderer.tapToLogical(.25f,.5f,x,y);renderer.tapToLogical(.75f,.875f,ex,ey);
    auto sample=input.getTouchSnapshot();
    assert(sample.count==4 && sample.reportedCount==5 && sample.idsStable);
    for(unsigned i=0;i<4;++i) assert(sample.contacts[i].id==10+i && sample.contacts[i].valid && sample.contacts[i].x==x && sample.contacts[i].y==y);
    MappedInputManager::CompletedSwipe swipe; assert(input.wasCompletedMultiTouchSwipe(swipe));
    assert(swipe.contactCount==2 && swipe.startX==x && swipe.startY==y && swipe.endX==ex && swipe.endY==ey && swipe.durationMs==123);
    MappedInputManager::CompletedTransform event; assert(input.wasCompletedMultiTouchRotation(event));
    assert(event.value==-33 && event.centerX==x && event.centerY==y && event.durationMs==234);
    assert(input.wasCompletedMultiTouchPinch(event));assert(event.value==1.3f && event.durationMs==345);
    gpio.event=false;assert(!input.wasCompletedMultiTouchPinch(event));gpio.event=true;
  }
  gpio.present=false;assert(!input.supportsMultiTouch());
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory); (p/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('Mapped touch adapter checks passed')
