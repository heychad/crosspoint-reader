"""Exercise the actual reader notice arbitration and timeout with host stubs."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/activities/reader/ReaderActivity.cpp').read_text()
epub=(root/'src/activities/reader/EpubReaderActivity.cpp').read_text()
epub_start=epub.index('void EpubReaderActivity::loop()')
epub_end=epub.index('  rememberBookOnceRendered();', epub_start)
epub_head=epub[epub_start:epub_end]+'}\n'
def extract(signature):
 start=source.index(signature);opening=source.index('{',start);depth=0
 for at in range(opening,len(source)):
  if source[at]=='{':depth+=1
  elif source[at]=='}':
   depth-=1
   if not depth:return source[start:at+1]
 raise ValueError(signature)
program=r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <StatsSaveNotice.h>
uint32_t settingsWord=0; StatsSaveNotice& settingsSaveNotice(){ static StatsSaveNotice n(settingsWord); return n; }
unsigned long now=0;unsigned long millis(){return now;}
enum class StrId {STR_HOME_BUTTON_READER_ON=1,STR_HOME_BUTTON_READER_OFF=2,STR_HOME_BUTTON_SAVE_FAILED=3};
#define STR_SETTINGS_SAVE_RECOVERY 12
#define STR_READER_STATS_SAVE_FAILED 10
#define STR_READER_STATS_UNAVAILABLE 11
#define tr(x) (x)
struct Strings {int get(StrId id){return static_cast<int>(id);}} I18N;
struct UI {int message=0;void drawPopup(int,int id){message=id;}} GUI;
struct ReaderActivity {
 std::atomic<uint32_t> shownSettingsNotice{0}; bool showPendingSettingsWarning();
 std::atomic<bool> settingsNoticeRenderReady{true};
 void pollSettingsSaveWarning();
 std::atomic<uint8_t> pendingHomeButtonFeedback{0};std::atomic<bool> homeButtonFeedbackVisible{false};
 std::atomic<unsigned long> homeButtonFeedbackStarted{0};std::atomic<bool> pendingStatsSaveWarning{false},pendingStatsLoadWarning{false};
 int renderer=0,updates=0; bool formatWarning=false;
 void requestUpdate(){++updates;}bool showPendingHomeButtonFeedback();bool showPendingStatsWarning();
 bool showPendingFormatWarning(){if(!formatWarning)return false;formatWarning=false;GUI.message=20;return true;}
 void showNextReaderNotice(bool);void expireHomeButtonFeedback();
};
struct Popup {bool active=true;bool isActive() const{return active;}template<class F>void handleInput(int,F){}};
struct EpubReaderActivity:ReaderActivity {Popup loadFailurePopup;int mappedInput=0;bool epub=true;bool handlePendingQuickAction(){return false;}void finish(){}void loop();};
'''+extract('void ReaderActivity::pollSettingsSaveWarning()')+'\n'+extract('bool ReaderActivity::showPendingSettingsWarning()')+'\n'+extract('bool ReaderActivity::showPendingStatsWarning()')+'\n'+extract('bool ReaderActivity::showPendingHomeButtonFeedback()')+'\n'+extract('void ReaderActivity::showNextReaderNotice(bool formatPopupDrawn)')+'\n'+extract('void ReaderActivity::expireHomeButtonFeedback()')+'\n'+epub_head+r'''
int main(){ReaderActivity r;now=100;r.pendingStatsSaveWarning=true;r.pendingStatsLoadWarning=true;r.formatWarning=true;r.pendingHomeButtonFeedback=3;
 r.showNextReaderNotice(false);assert(GUI.message==10 && !r.pendingStatsSaveWarning && r.pendingStatsLoadWarning && r.formatWarning && r.pendingHomeButtonFeedback==3);
 now+=1200;r.expireHomeButtonFeedback();r.showNextReaderNotice(false);assert(GUI.message==11 && r.formatWarning && r.pendingHomeButtonFeedback==3);
 now+=1200;r.expireHomeButtonFeedback();r.showNextReaderNotice(false);assert(GUI.message==20 && r.pendingHomeButtonFeedback==3);
 now+=1200;r.expireHomeButtonFeedback();r.showNextReaderNotice(false);assert(GUI.message==3 && r.pendingHomeButtonFeedback==0);
 now+=1200;r.expireHomeButtonFeedback();r.pendingHomeButtonFeedback=1;r.showNextReaderNotice(true);assert(r.pendingHomeButtonFeedback==1);
 now+=1200;r.expireHomeButtonFeedback();r.showNextReaderNotice(false);assert(GUI.message==1 && r.pendingHomeButtonFeedback==0);
 now+=1200;r.expireHomeButtonFeedback();r.pendingHomeButtonFeedback=2;r.showNextReaderNotice(false);assert(GUI.message==2);
 now+=1199;int updates=r.updates;r.expireHomeButtonFeedback();assert(r.homeButtonFeedbackVisible && r.updates==updates);
 now+=1;r.expireHomeButtonFeedback();assert(!r.homeButtonFeedbackVisible && r.updates==updates+1);
 settingsSaveNotice().recordFailure();int before=r.updates;r.pollSettingsSaveWarning();assert(r.updates==before+1);r.showNextReaderNotice(false);assert(GUI.message==12);auto first=r.shownSettingsNotice.load();assert(first&&settingsSaveNotice().pending()==first);
 settingsSaveNotice().recordFailure();r.showNextReaderNotice(false);assert(r.shownSettingsNotice.load()==first);now+=1200;r.expireHomeButtonFeedback();r.showNextReaderNotice(false);assert(GUI.message==12&&r.shownSettingsNotice.load()!=first);
 EpubReaderActivity e;for(int i=0;i<100;++i)e.loop();assert(e.updates==0);e.loadFailurePopup.active=false;e.loop();assert(e.updates==1);
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'test.cpp';p.write_text(program);exe=Path(tmp)/'test'
 subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(root/'src/activities/reader'),str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
print('Reader notice priority, deferral, feedback, and timeout: passed')
