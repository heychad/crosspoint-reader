"""Run the production pending-command gate against controlled render generations."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/activities/reader/ReaderQuickActions.cpp').read_text()
method='bool ReaderActivity::handlePendingQuickAction()'+source.split('bool ReaderActivity::handlePendingQuickAction()',1)[1]
code=r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include "personalization/ReaderShortcuts.h"
using readershortcuts::Action;
bool lockAvailable=true;
struct RenderLock {enum class Mode{Try};explicit RenderLock(Mode){}bool ownsLock()const{return lockAvailable;}};
struct MappedInputManager {enum class Button{Back};bool back=false;bool wasReleased(Button)const{return back;}};
enum class ReadingStatsFrame{Page,EndOfBook};
struct Stats {int resumed=0;ReadingStatsFrame frame=ReadingStatsFrame::Page;void onRender(ReadingStatsFrame f){++resumed;frame=f;}};
class ReaderActivity{
public:
  std::atomic<Action>pendingQuickAction{Action::Default};
  uint32_t renderedPageGeneration=0,quickActionPaintGeneration=0;
  MappedInputManager mappedInput;Stats readingStats;bool end=false;int calls=0;Action dispatched=Action::Default;
  bool isAtEndOfBook()const{return end;}
  void handleReaderShortcut(Action a){assert(readingStats.resumed>0);++calls;dispatched=a;}
  bool handlePendingQuickAction();
};
''' + method+r'''
int main(){
 ReaderActivity r;assert(!r.handlePendingQuickAction());
 r.pendingQuickAction=Action::Brighter;r.quickActionPaintGeneration=3;r.renderedPageGeneration=3;
 assert(r.handlePendingQuickAction());assert(r.calls==0 && r.readingStats.resumed==0);
 r.renderedPageGeneration=4;lockAvailable=false;assert(r.handlePendingQuickAction());assert(r.calls==0);
 lockAvailable=true;assert(r.handlePendingQuickAction());assert(r.calls==1 && r.dispatched==Action::Brighter && r.readingStats.resumed==1);
 assert(!r.handlePendingQuickAction());assert(r.calls==1);
 r.pendingQuickAction=Action::Menu;r.mappedInput.back=true;assert(!r.handlePendingQuickAction());assert(r.calls==1);
 r.mappedInput.back=false;r.pendingQuickAction=Action::Refresh;r.quickActionPaintGeneration=UINT32_MAX;r.renderedPageGeneration=0;r.end=true;
 assert(r.handlePendingQuickAction());assert(r.calls==2 && r.readingStats.frame==ReadingStatsFrame::EndOfBook);
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++20','-I',str(root/'src'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Deferred Quick Actions dispatch checks passed')
