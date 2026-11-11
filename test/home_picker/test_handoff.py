"""Exercise the real Home action picker and EPUB menu handoff."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
settings = (root / 'src/activities/settings/HomeButtonSettingsActivity.cpp').read_text()
start = settings.index('void HomeButtonSettingsActivity::activateIndex(')
end = settings.index('\nvoid HomeButtonSettingsActivity::buildScreen(', start)
activate = settings[start:end]
reader = (root / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
start = reader.index('    case EpubReaderMenuActivity::MenuAction::HOME_BUTTON:')
end = reader.index('    case EpubReaderMenuActivity::MenuAction::', start + 10)
menu = reader[start:end]
code = r'''
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <utility>
#include <variant>
#define LOG_ERR(...) ((void)0)
bool failAllocation=false;
enum class HomeButtonAction { Count=26 };
using StrId=int;
struct Settings { uint8_t tap=0, twice=0, hold=0; int saves=0; void saveToFile(){++saves;} } SETTINGS;
namespace home_button {
 constexpr StrId ACTION_LABELS[26] = {};
 constexpr StrId GESTURE_LABELS[3] = {};
 constexpr uint8_t Settings::* FIELDS[] = {&Settings::tap,&Settings::twice,&Settings::hold};
}
namespace readerquickactions {int calls=0;void normalizeBindings(Settings&,uint8_t Settings::*){++calls;}}
struct MenuResult { int action; };
struct ActivityResult {bool isCancelled=false;std::variant<std::monostate,MenuResult> data;};
struct Input {int resets=0;void resetHomeButtonInput(){++resets;}};
struct App {void clearTapFlash(){}};
struct EnumSelectionActivity {
 int selected; size_t count;
 EnumSelectionActivity(int&,Input&,StrId,std::span<const StrId> labels,int current):selected(current),count(labels.size()){}
};
template<class T,class... A> std::unique_ptr<T> makeUniqueNoThrow(A&&... a) {
 if(failAllocation) return {}; return std::make_unique<T>(std::forward<A>(a)...);
}
struct HomeButtonSettingsActivity {
 int renderer=0, updates=0;Input mappedInput;App app;int nav_selected=0;
 HomeButtonSettingsActivity()=default;
 HomeButtonSettingsActivity(int&,Input&){}
 struct {int selected=0;} nav;
 std::function<void(const ActivityResult&)> callback;std::unique_ptr<EnumSelectionActivity> child;
 int listCount()const{return 3;}void requestUpdate(){++updates;}
 template<class F>void startActivityForResult(std::unique_ptr<EnumSelectionActivity> c,F&& f){child=std::move(c);callback=std::forward<F>(f);}
 void activateIndex(int);
};
''' + activate + r'''
struct EpubReaderMenuActivity {enum class MenuAction {HOME_BUTTON, OTHER};};
struct Reader {
 int renderer=0,updates=0;Input mappedInput;bool opened=false;std::function<void(const ActivityResult&)> callback;
 void requestUpdate(){++updates;}
 template<class F>void startActivityForResult(std::unique_ptr<HomeButtonSettingsActivity> c,F&& f){assert(c);opened=true;callback=std::forward<F>(f);}
 void open(){switch(EpubReaderMenuActivity::MenuAction::HOME_BUTTON){
''' + menu + r'''
 default:break;}}
};
int main(){
 for(int i=0;i<26;++i){
  HomeButtonSettingsActivity screen;SETTINGS.tap=24;int saves=SETTINGS.saves, calls=readerquickactions::calls;
  screen.activateIndex(0);assert(screen.child && screen.child->count==26 && screen.child->selected==24);
  screen.callback(ActivityResult{false,MenuResult{i}});
  assert(SETTINGS.tap==i && SETTINGS.saves==saves+1 && readerquickactions::calls==calls+1 && screen.updates==1);
 }
 HomeButtonSettingsActivity screen;SETTINGS.twice=3;screen.activateIndex(1);int saves=SETTINGS.saves;
 screen.callback(ActivityResult{true,MenuResult{18}});assert(SETTINGS.twice==3 && SETTINGS.saves==saves);
 screen.callback(ActivityResult{false,MenuResult{26}});screen.callback(ActivityResult{false,MenuResult{-1}});
 screen.callback(ActivityResult{});assert(SETTINGS.twice==3 && SETTINGS.saves==saves);
 HomeButtonSettingsActivity invalid;invalid.activateIndex(3);assert(!invalid.child);
 failAllocation=true;HomeButtonSettingsActivity oom;oom.activateIndex(0);assert(!oom.child && oom.updates==1);
 Reader failed;failed.open();assert(!failed.opened && failed.updates==1);
 failAllocation=false;Reader ok;ok.open();assert(ok.opened && ok.callback);ok.callback({});assert(ok.updates==1);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20',str(tmp / 'test.cpp'),'-o',str(tmp / 'test')],check=True)
    subprocess.run([str(tmp / 'test')],check=True)
print('All Home assignments, cancellation, OOM, and reader handoff passed')
