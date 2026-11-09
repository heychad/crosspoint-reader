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
#define LOG_ERR(...) ((void)0)
bool failAllocation=false;
enum class StrId { STR_PROGRESS_BAR, STR_PROGRESS_BAR_THICKNESS, STR_BAR_PERCENT_FORMAT };
enum class ReaderBarItem:uint8_t { Empty, Clock, Battery, ChapterPages, BookPercent, BookTitle, ChapterTitle, Date, TimeLeftBook, TimeLeftSection, Count };
const StrId SLOT_LABELS[7]={};const StrId ITEM_LABELS[10]={};const StrId PROGRESS_LABELS[3]={},THICKNESS_LABELS[3]={},DECIMAL_LABELS[3]={};
struct ReaderBar {std::array<ReaderBarItem,7> slots{};int progress=0,thickness=0,percentageDecimals=0;};
struct Settings {bool readerBarsEnabled=false;int saves=0;void saveToFile(){++saves;}} SETTINGS;
struct MenuResult{int action;};struct ActivityResult{bool isCancelled=false;std::variant<std::monostate,MenuResult> data;};
struct Popup {bool isActive(){return false;}template<class... A>void show(A&&...) {}};
struct App{void clearTapFlash(){}};struct Nav{void reset(){}};struct Input{};
struct EnumSelectionActivity {
 size_t count;int current;
 EnumSelectionActivity(int&,Input&,StrId,std::span<const StrId> items,int c):count(items.size()),current(c){}
};
template<class T,class... A>std::unique_ptr<T> makeUniqueNoThrow(A&&... a){if(failAllocation)return{};return std::make_unique<T>(std::forward<A>(a)...);}
struct ReaderBarsSettingsActivity {
 enum class View{Root,Top,Bottom};View view=View::Top;int renderer=0,updates=0;Input mappedInput;App app;Nav nav;Popup popup;
 ReaderBar top,bottom;std::unique_ptr<EnumSelectionActivity> child;std::function<void(const ActivityResult&)> callback;
 ReaderBar& bar(){return view==View::Top?top:bottom;}int listCount(){return 10;}void requestUpdate(){++updates;}
 template<class F>void startActivityForResult(std::unique_ptr<EnumSelectionActivity> c,F f){child=std::move(c);callback=f;}
 void activateIndex(int index);
};
''' + body + r'''
int main(){
 ReaderBarsSettingsActivity screen;screen.activateIndex(3);assert(screen.child && screen.child->count==10);
 screen.callback({false,MenuResult{9}});assert(screen.top.slots[3]==ReaderBarItem::TimeLeftSection && SETTINGS.saves==1);
 screen.callback({true,MenuResult{1}});screen.callback({false,MenuResult{10}});screen.callback({false,MenuResult{-1}});
 assert(screen.top.slots[3]==ReaderBarItem::TimeLeftSection && SETTINGS.saves==1);
 screen.view=ReaderBarsSettingsActivity::View::Bottom;screen.callback({false,MenuResult{1}});
 assert(screen.bottom.slots[3]==ReaderBarItem::Empty && SETTINGS.saves==1);
 failAllocation=true;ReaderBarsSettingsActivity oom;oom.activateIndex(0);assert(!oom.child && oom.updates==1);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp);(tmp/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20',str(tmp/'test.cpp'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('Reader bar slot selection and failure paths passed')
