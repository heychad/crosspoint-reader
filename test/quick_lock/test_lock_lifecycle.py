"""Exercise production lock lifecycle and reader accounting with a bounded renderer double."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
manager=(root/'src/activities/ActivityManager.cpp').read_text()
methods=manager[manager.index('bool ActivityManager::canApplyInputLock()'):manager.index('bool ActivityManager::dispatchShortcut(')]
reader=(root/'src/activities/reader/ReaderActivity.cpp').read_text()
hook=reader[reader.index('void ReaderActivity::onInputLockChanged('):]
renderer=r'''
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
class GfxRenderer {
public:
 enum Orientation{Portrait,LandscapeClockwise,PortraitInverted,LandscapeCounterClockwise};
 Orientation orientation=Portrait;int displays=0;mutable std::vector<uint8_t>pixels=std::vector<uint8_t>(800*800,1);
 Orientation getOrientation()const{return orientation;}
 int getScreenWidth()const{return orientation%2?800:480;}int getScreenHeight()const{return orientation%2?480:800;}
 void getOrientedViewableTRBL(int*t,int*r,int*b,int*l)const{*t=3;*r=5;*b=7;*l=9;}
 size_t getRegionByteSize(int,int,int w,int h)const{return ((w+7)/8)*h;}
 bool copyRegionToBuffer(int x,int y,int w,int h,uint8_t*b,size_t n)const{
  if(n<getRegionByteSize(x,y,w,h))return false;std::fill(b,b+n,0);
  for(int j=0;j<h;++j)for(int i=0;i<w;++i)if(pixels[(y+j)*800+x+i])b[j*((w+7)/8)+i/8]|=1<<(i%8);return true;
 }
 bool copyBufferToRegion(int x,int y,int w,int h,const uint8_t*b,size_t n)const{
  if(n<getRegionByteSize(x,y,w,h))return false;
  for(int j=0;j<h;++j)for(int i=0;i<w;++i)pixels[(y+j)*800+x+i]=(b[j*((w+7)/8)+i/8]>>(i%8))&1;return true;
 }
 void fillRect(int x,int y,int w,int h,bool value)const{for(int j=0;j<h;++j)for(int i=0;i<w;++i)pixels.at((y+j)*800+x+i)=value;}
 void drawRect(int x,int y,int w,int h,int,bool value)const{fillRect(x,y,w,h,value);}
 void displayBuffer(int){++displays;}
};
'''
code=r'''
#include <cassert>
#include <atomic>
#include <memory>
#include <vector>
#include "personalization/LockBadge.h"
int locks=0;
struct RenderLock{RenderLock(){++locks;}~RenderLock(){--locks;}};
namespace HalPowerManager{struct Lock{};}
struct HalDisplay{enum{FAST_REFRESH};};
struct Light{bool on=true;bool isOn()const{return on;}void setOn(bool b){assert(locks);on=b;}}Frontlight;
struct Input{int homes=0,touches=0;void resetHomeButtonInput(){assert(locks);++homes;}void suppressTouchContact(){assert(locks);++touches;}};
struct Activity{int stops=0,starts=0;bool busy=false,exclusive=false;virtual ~Activity()=default;
 virtual void onInputLockChanged(bool value){assert(locks);value?++stops:++starts;}
 bool preventAutoSleep(){return busy;}bool requiresExclusiveStorageLoop(){return exclusive;}};
enum class ReadingStatsFrame{Page,EndOfBook};
struct Stats{int stops=0,starts=0;void pause(){assert(locks);++stops;}void onRender(ReadingStatsFrame){assert(locks);++starts;}};
struct Session{int starts=0;void onRenderComplete(unsigned,long,int){assert(locks);++starts;}};
unsigned millis(){return 100;}namespace trustedtime{long trustedNow(){return 200;}}
struct ReaderActivity:Activity{Stats readingStats;Session readerSession;std::atomic<bool>pageRendered{true};bool overlay=false;int cancelled=0,stopped=0,flushed=0;
 void finishReaderGestures(){assert(locks);++cancelled;}void onReaderGestureStarted(){assert(locks);++stopped;}void flushReaderSession(){assert(locks);++flushed;}
 bool inputOverPage(){return overlay;}bool isAtEndOfBook(){return false;}int getProgressBasisPoints(){return 2500;}
 void onInputLockChanged(bool)override;};
struct ActivityManager{
 GfxRenderer renderer;Input mappedInput;std::vector<std::unique_ptr<Activity>>stackActivities;std::unique_ptr<Activity>currentActivity;
 enum class PendingAction{None,Push};PendingAction pendingAction=PendingAction::None;
 std::atomic<bool>inputLocked{false},renderInFlight{false};LockBadge lockBadge;bool lockedRenderPending=false,lockLightWasOn=false,update=false;
 bool isInputLocked()const{return inputLocked.load();}void requestUpdate(){update=true;}
 bool canApplyInputLock()const;bool applyInputLock(bool,bool=true);
};
''' + methods+hook+r'''
int main(){
 for(int orientation=0;orientation<4;++orientation){
  ActivityManager m;m.renderer.orientation=static_cast<GfxRenderer::Orientation>(orientation);
  auto child=std::make_unique<ReaderActivity>();auto*r=child.get();m.currentActivity=std::move(child);
  auto parent=std::make_unique<ReaderActivity>();auto*p=parent.get();m.stackActivities.push_back(std::move(parent));
  auto pixels=m.renderer.pixels;Frontlight.on=true;assert(m.canApplyInputLock());
  m.renderInFlight=true;assert(!m.canApplyInputLock());assert(!m.applyInputLock(true));assert(!m.isInputLocked());assert(Frontlight.on);m.renderInFlight=false;
  m.applyInputLock(true);assert(!Frontlight.on && m.isInputLocked());assert(m.renderer.pixels!=pixels);
  assert(r->readingStats.stops==1 && p->readingStats.stops==1 && r->flushed==1);
  m.applyInputLock(true);assert(r->readingStats.stops==1);
  m.applyInputLock(false);assert(Frontlight.on && !m.isInputLocked());assert(m.renderer.pixels==pixels);
  assert(r->readingStats.starts==1 && r->readerSession.starts==1 && p->readingStats.starts==0);
  assert(m.renderer.displays==2 && !m.update);
  Frontlight.on=false;m.applyInputLock(true);m.applyInputLock(false);assert(!Frontlight.on);
  m.applyInputLock(true);m.lockedRenderPending=true;m.applyInputLock(false);assert(m.update);m.update=false;
  Frontlight.on=true;m.applyInputLock(true);auto starts=r->readingStats.starts;m.applyInputLock(false,false);
  assert(!Frontlight.on && !m.isInputLocked() && r->readingStats.starts==starts);
  assert(m.renderer.pixels==pixels);
  r->busy=true;assert(!m.canApplyInputLock());r->busy=false;r->exclusive=true;assert(!m.canApplyInputLock());
  r->exclusive=false;m.pendingAction=ActivityManager::PendingAction::Push;assert(!m.canApplyInputLock());
 }
 ReaderActivity r;{RenderLock lock;r.overlay=true;r.onInputLockChanged(false);assert(r.readingStats.starts==0);r.overlay=false;r.pageRendered=false;r.onInputLockChanged(false);assert(r.readingStats.starts==0);}
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'GfxRenderer.h').write_text(renderer);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++20','-I',str(p),'-I',str(root/'src'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Four-orientation badge restore, lock lifecycle, frontlight and reader-accounting checks passed')
