"""Exercise the EPUB Controls handoff, including allocation failure and return."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
start = source.index('    case EpubReaderMenuActivity::MenuAction::CONTROLS:')
end = source.index('    case EpubReaderMenuActivity::MenuAction::', start + 10)
action = source[start:end]
code = r'''
#include <cassert>
#include <functional>
#include <memory>
#include <utility>
#define LOG_ERR(...) ((void)0)
bool failAllocation=false;
enum class SettingsSection {Controls};
struct Input {};
struct SettingsActivity {SettingsSection section;SettingsActivity(int&,Input&,SettingsSection value):section(value){}};
template<class T,class... A>std::unique_ptr<T> makeUniqueNoThrow(A&&... a){
 if(failAllocation)return {};return std::make_unique<T>(std::forward<A>(a)...);
}
struct ActivityResult {};
struct EpubReaderMenuActivity {enum class MenuAction {CONTROLS};};
struct Reader {
 int renderer=0,menus=0;Input mappedInput;bool opened=false;std::function<void(const ActivityResult&)> callback;
 void openReaderMenu(){++menus;}
 template<class F>void startActivityForResult(std::unique_ptr<SettingsActivity> screen,F&& f){
  assert(screen && screen->section==SettingsSection::Controls);opened=true;callback=std::forward<F>(f);
 }
 void open(){switch(EpubReaderMenuActivity::MenuAction::CONTROLS){
''' + action + r'''
 }}
};
int main(){
 Reader ok;ok.open();assert(ok.opened && ok.menus==0 && ok.callback);ok.callback({});assert(ok.menus==1);
 failAllocation=true;Reader failed;failed.open();assert(!failed.opened && failed.menus==1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-fsanitize=address', '-fno-omit-frame-pointer',
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Reader Controls handoff passed under AddressSanitizer')
