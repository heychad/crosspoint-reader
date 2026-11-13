"""Exercise the production reader-bar slot picker and result validation."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
source=(root/'src/activities/settings/ReaderBarsSettingsActivity.cpp').read_text()
start=source.index('void ReaderBarsSettingsActivity::activateIndex(')
end=source.index('\nvoid ReaderBarsSettingsActivity::',start+10)
body=source[start:end]
code=r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <utility>
#include <variant>
#include "personalization/ReaderBars.h"
#define tr(key) "label"
#define LOG_ERR(...) ((void)0)
bool failAllocation=false;
enum class StrId { STR_PROGRESS_BAR, STR_PROGRESS_BAR_THICKNESS, STR_BAR_PERCENT_FORMAT, STR_BAR_BATTERY_STYLE, STR_STATUS_BAR_TEXT_SIZE, STR_XTC_STATUS_BAR };
const StrId SLOT_LABELS[7]={};const StrId ITEM_LABELS[static_cast<unsigned>(ReaderBarItem::Count)]={};
const StrId PROGRESS_LABELS[3]={},THICKNESS_LABELS[3]={},DECIMAL_LABELS[3]={},BATTERY_LABELS[4]={},SIZE_LABELS[3]={},XTC_LABELS[4]={};
struct Settings {bool legacyXtcTopUsesBottom=true;unsigned xtcStatusBarMode=2;unsigned statusBarTextSize=0;bool readerBarsEnabled=false;int saves=0;void saveToFile(){++saves;}} SETTINGS;
struct MenuResult{int action;};struct ActivityResult{bool isCancelled=false;std::variant<std::monostate,MenuResult> data;};
struct Popup {std::function<void(int)> callback;bool isActive(){return false;}template<class T,class O>void show(T,O,int,int,std::function<void(int)> action) {callback=action;}};
struct App{void clearTapFlash(){}};struct Nav{void reset(){}};struct Input{};
struct EnumSelectionActivity {
 size_t count;int current;
 EnumSelectionActivity(int&,Input&,StrId,std::span<const StrId> items,int c):count(items.size()),current(c){}
};
template<class T,class... A>std::unique_ptr<T> makeUniqueNoThrow(A&&... a){if(failAllocation)return{};return std::make_unique<T>(std::forward<A>(a)...);}
struct ReaderBarsSettingsActivity {
 enum class View{Root,Top,Bottom};View view=View::Top;int renderer=0,updates=0;Input mappedInput;App app;Nav nav;Popup popup;
 ReaderBar top,bottom;std::unique_ptr<EnumSelectionActivity> child;std::function<void(const ActivityResult&)> callback;
 ReaderBar& bar(){return view==View::Top?top:bottom;}int listCount(){return view==View::Root?5:14;}void saveBar(bool layoutChanged=true){if(layoutChanged&&view==View::Top)SETTINGS.legacyXtcTopUsesBottom=false;SETTINGS.saveToFile();}void requestUpdate(){++updates;}
 template<class F>void startActivityForResult(std::unique_ptr<EnumSelectionActivity> c,F f){child=std::move(c);callback=f;}
 void activateIndex(int index);
};
''' + body + r'''
int main(){
 ReaderBarsSettingsActivity screen;screen.activateIndex(3);assert(screen.child && screen.child->count==static_cast<unsigned>(ReaderBarItem::Count));
 screen.callback({false,MenuResult{9}});assert(screen.top.slots[3]==ReaderBarItem::TimeLeftSection && SETTINGS.saves==1);
 screen.callback({true,MenuResult{1}});screen.callback({false,MenuResult{static_cast<int>(ReaderBarItem::Count)}});screen.callback({false,MenuResult{-1}});
 assert(screen.top.slots[3]==ReaderBarItem::TimeLeftSection && SETTINGS.saves==1);
 screen.view=ReaderBarsSettingsActivity::View::Bottom;screen.callback({false,MenuResult{1}});
 assert(screen.bottom.slots[3]==ReaderBarItem::Empty && SETTINGS.saves==1);
 SETTINGS.legacyXtcTopUsesBottom=true;screen.view=ReaderBarsSettingsActivity::View::Top;screen.activateIndex(10);assert(screen.top.hidden && SETTINGS.saves==2 && SETTINGS.legacyXtcTopUsesBottom);
 screen.activateIndex(11);assert(screen.top.centered && SETTINGS.saves==3 && !SETTINGS.legacyXtcTopUsesBottom);
 screen.activateIndex(12);screen.popup.callback(3);assert(screen.top.batteryStyle==ReaderBarBatteryStyle::PercentOnly && SETTINGS.saves==4);
 screen.activateIndex(13);screen.popup.callback(2);assert(screen.top.separator==ReaderBarSeparator::Pipe && SETTINGS.saves==5);
 ReaderBarsSettingsActivity rootScreen;rootScreen.view=ReaderBarsSettingsActivity::View::Root;
 rootScreen.activateIndex(3);assert(rootScreen.popup.callback);
 for(int size:{0,1,2}){rootScreen.popup.callback(size);assert(SETTINGS.statusBarTextSize==static_cast<unsigned>(size));}
 const int saves=SETTINGS.saves;
 for(int invalid:{-1,3,255})rootScreen.popup.callback(invalid);
 assert(SETTINGS.statusBarTextSize==2 && SETTINGS.saves==saves);
 rootScreen.activateIndex(4);rootScreen.popup.callback(3);assert(SETTINGS.xtcStatusBarMode==3 && !SETTINGS.legacyXtcTopUsesBottom);
 SETTINGS.legacyXtcTopUsesBottom=true;ReaderBarsSettingsActivity unchanged;unchanged.activateIndex(0);
 unchanged.callback({false,MenuResult{0}});assert(SETTINGS.legacyXtcTopUsesBottom);
 failAllocation=true;ReaderBarsSettingsActivity oom;oom.activateIndex(0);assert(!oom.child && oom.updates==1);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp);(tmp/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20','-I',str(root/'src'),str(tmp/'test.cpp'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('Reader bar slot selection and failure paths passed')
