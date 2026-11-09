"""Run the production reader Back handler with a shared-timer overlap."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'src/activities/reader/ReaderUtils.h').read_text()
method=s[s.index('inline bool handleBackNavigation('):s.index('}  // namespace ReaderUtils')]
code=r'''
#include <cassert>
struct Settings{int backHoldAction=0;bool backShortToFileBrowser=false;} SETTINGS;
constexpr unsigned GO_BACK_OR_HOME_MS=1000;
struct MappedInputManager{enum class Button{Back};bool down=false,released=false;unsigned duration=2000;mutable int polls=0;bool wasBackGesture()const{return false;}bool wasLongPressed(Button,unsigned)const{++polls;return down;}bool wasReleased(Button)const{return released;}unsigned getHeldTime()const{return duration;}};
struct ActivityManager{int files=0;void goToFileBrowser(const char*){++files;}};
struct BackNavCallback{void*ctx;void(*fn)(void*);};
'''+method+r'''
int main(){MappedInputManager input;ActivityManager manager;int homes=0;BackNavCallback home{&homes,[](void*p){++*static_cast<int*>(p);}};
 SETTINGS.backHoldAction=34;input.down=true;assert(!handleBackNavigation(input,manager,"",home));assert(input.polls==0);
 input.down=false;input.released=true;assert(handleBackNavigation(input,manager,"",home));assert(homes==1 && manager.files==0);
 SETTINGS.backHoldAction=0;assert(handleBackNavigation(input,manager,"",home));assert(manager.files==1);
 SETTINGS.backHoldAction=34;SETTINGS.backShortToFileBrowser=true;assert(handleBackNavigation(input,manager,"",home));assert(manager.files==2);
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++20',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Reader Back override preserves short release despite shared hold timer')
