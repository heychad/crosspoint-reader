"""A Home tap begun in the end menu must not fire on the restored page."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/reader/ReaderActivity.cpp').read_text()
start = source.index('bool ReaderActivity::handleEndOfBookMenu(')
end = source.index('\nbool ReaderActivity::handleEndOfBookPageTurn(', start)
handler = source[start:end]
loop_start = source.index('void ReaderActivity::loop()')
loop_end = source.index('\nvoid ReaderActivity::showPendingStatsWarning(', loop_start)
loop = source[loop_start:loop_end]
code = r'''
#include <cassert>
#include <memory>
#include <string>
#include "util/HomeButtonInput.h"
struct Input {
 HomeButtonInput home; int resets=0; bool down=false;
 void resetHomeButtonInput(){home.discardContact(down);++resets;}
 HomeButtonAction tick(unsigned now,bool tap,bool held=false,bool pressed=false){return home.update(now,tap,held,false,pressed,HomeButtonAction::Library,HomeButtonAction::SavedItems,HomeButtonAction::FileTransfer,down);}
};
struct EndOfBookOptions {
 enum class Action {OpenBook,GoHome,LastPage,Redraw,None};Action action=Action::LastPage;
 Action handleMenuInput(Input&,std::string*){return action;}
};
struct Manager {void goToReader(const std::string&){} };
struct ReaderActivity {
 Input mappedInput;Manager activityManager;std::unique_ptr<EndOfBookOptions> endOfBookOptions=std::make_unique<EndOfBookOptions>();
 bool active=true,returned=false;
 bool endOfBookMenuActive(){return active;}
 void onReturnFromEndOfBook(){returned=true;active=false;}
 void onGoHome(){}
 void requestUpdate(){}
 bool handleEndOfBookMenu(bool suppressConfirmRelease=false);
};
''' + handler + r'''
int main(){
 ReaderActivity reader;
 assert(reader.mappedInput.tick(0,true)==HomeButtonAction::Ignore);
 assert(reader.handleEndOfBookMenu());
 assert(reader.returned && reader.mappedInput.resets==1);
 assert(reader.mappedInput.tick(500,false)==HomeButtonAction::Ignore);
 ReaderActivity noTransition;noTransition.endOfBookOptions->action=EndOfBookOptions::Action::None;
 assert(noTransition.mappedInput.tick(0,true)==HomeButtonAction::Ignore);
 assert(!noTransition.handleEndOfBookMenu());assert(noTransition.mappedInput.resets==0);
 ReaderActivity held;held.mappedInput.down=true;
 assert(held.mappedInput.tick(0,false,false,true)==HomeButtonAction::Ignore);
 assert(held.handleEndOfBookMenu());
 assert(held.mappedInput.tick(800,false,true)==HomeButtonAction::Ignore);
 held.mappedInput.down=false;assert(held.mappedInput.tick(900,true)==HomeButtonAction::Ignore);
 held.mappedInput.down=true;assert(held.mappedInput.tick(1000,false,false,true)==HomeButtonAction::Ignore);
 held.mappedInput.down=false;assert(held.mappedInput.tick(1100,true)==HomeButtonAction::Ignore);
 assert(held.mappedInput.tick(1500,false)==HomeButtonAction::Library);
}
'''
loop_code = r'''
#include <cassert>
#include <tuple>
struct Input { unsigned long getHeldTime() const { return 0; } };
struct Settings { int longPressButtonBehavior=0; const int CHAPTER_SKIP=1; } SETTINGS;
namespace ReaderUtils {
 struct Touch { bool prev=false,next=false; unsigned long heldMs=0; };
 constexpr int SKIP_HOLD_MS=1000;
 Touch detectTouchPageTurn(int&, Input&) { return {}; }
 std::tuple<bool,bool,bool> detectPageTurn(Input&) { return {}; }
}
namespace haptic_feedback { void touchAction(bool) {} }
struct ReaderActivity {
 int renderer=0, shortcuts=0; Input mappedInput; bool active=true;
 bool handlePendingQuickAction(){return false;}
 void rememberBookOnceRendered(){} void clearEndOfBookOptionsIfNeeded(){}
 bool handleEndOfBookMenu(){return false;} bool handleFormatInput(){return false;}
 bool endOfBookMenuActive(){return active;}
 bool handleHomeReaderShortcut(){++shortcuts;return true;}
 bool handleBackNavigation(){return false;}
 bool handleEndOfBookPageTurn(bool,bool){return true;}
 bool skipPages(int){return false;} bool pageTurn(bool){return false;}
 void notePageTurn(bool,bool){} void requestUpdate(){}
 void loop();
};
''' + loop + r'''
int main() {
 ReaderActivity reader; reader.loop(); assert(reader.shortcuts == 0);
 reader.active=false; reader.loop(); assert(reader.shortcuts == 1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / 'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20','-I',str(root / 'src'),str(directory / 'test.cpp'),'-o',str(directory / 'test')],check=True)
    subprocess.run([str(directory / 'test')],check=True)
    (directory / 'loop.cpp').write_text(loop_code)
    subprocess.run(['c++','-std=c++20',str(directory / 'loop.cpp'),'-o',str(directory / 'loop')],check=True)
    subprocess.run([str(directory / 'loop')],check=True)
print('A delayed Home tap does not cross the end-menu transition')
