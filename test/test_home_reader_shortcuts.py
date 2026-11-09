"""Check Home-key mapping, dispatch guards, and reader-loop ordering."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/reader/ReaderQuickActions.cpp').read_text()
start = source.index('bool ReaderActivity::handleHomeReaderShortcut()')
end = source.index('\nbool ReaderActivity::supportsReaderShortcut(', start)
handler = source[start:end]
program = r'''
#include <atomic>
#include <cassert>
#include "personalization/HomeReaderShortcuts.h"
using readershortcuts::Action;
struct Input {HomeButtonAction action=HomeButtonAction::Ignore;HomeButtonAction homeButtonAction(){return action;}};
struct ReaderActivity;
bool lockHeld=false;
struct RenderLock {explicit RenderLock(ReaderActivity&){assert(!lockHeld);lockHeld=true;}~RenderLock(){lockHeld=false;}};
struct ReaderActivity {
 Input mappedInput;std::atomic<bool> pageRendered{false};bool overlay=false,supported=true;int calls=0;Action last=Action::Default;
 bool inputOverPage(){return overlay;}
 bool supportsReaderShortcut(Action){return supported;}
 bool handleReaderShortcut(Action action){assert(!lockHeld);++calls;last=action;return true;}
 bool handleHomeReaderShortcut();
};
''' + handler + r'''
int main(){
 using H=HomeButtonAction;
 assert(homereadershortcuts::actionFor(H::Home)==Action::Default);
 assert(homereadershortcuts::actionFor(H::QuickActions)==Action::Default);
 assert(homereadershortcuts::actionFor(H::FileBrowser)==Action::FileBrowser);
 assert(homereadershortcuts::actionFor(H::Library)==Action::Library);
 assert(homereadershortcuts::actionFor(H::SavedItems)==Action::SavedItems);
 assert(homereadershortcuts::actionFor(H::FileTransfer)==Action::FileTransfer);
 assert(homereadershortcuts::actionFor(H::ReadingStatistics)==Action::ReadingStatistics);
 ReaderActivity r;assert(!r.handleHomeReaderShortcut());
 r.mappedInput.action=H::Library;assert(r.handleHomeReaderShortcut() && r.calls==0);
 r.pageRendered=true;r.overlay=true;assert(r.handleHomeReaderShortcut() && r.calls==0);
 r.overlay=false;r.supported=false;assert(r.handleHomeReaderShortcut() && r.calls==0);
 r.supported=true;assert(r.handleHomeReaderShortcut() && r.calls==1 && r.last==Action::Library);
 r.mappedInput.action=H::FileTransfer;assert(r.handleHomeReaderShortcut() && r.calls==2 && r.last==Action::FileTransfer);
}
'''
reader = (root / 'src/activities/reader/ReaderActivity.cpp').read_text()
epub = (root / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
base_loop = reader[reader.index('void ReaderActivity::loop()'):reader.index('\nvoid ReaderActivity::render(', reader.index('void ReaderActivity::loop()'))]
epub_loop = epub[epub.index('void EpubReaderActivity::loop()'):]
assert base_loop.index('handleEndOfBookMenu()') < base_loop.index('handleHomeReaderShortcut()')
assert epub_loop.index('overlay != Overlay::None') < epub_loop.index('handleHomeReaderShortcut()')
assert 'if (!endOfBookMenuOpen && handleHomeReaderShortcut()) return;' in epub_loop
for transition in epub.split('overlay = Overlay::None;')[:-1]:
    assert transition.rstrip().endswith('mappedInput.resetHomeButtonInput();')
for action in ('FileBrowser','Library','SavedItems','FileTransfer','ReadingStatistics'):
    assert f'case HomeButtonAction::{action}:' in epub_loop[:epub_loop.index('if (automaticPageTurnActive)')]
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / 'test.cpp').write_text(program)
    subprocess.run(['c++','-std=c++20','-I',str(root / 'src'),str(directory / 'test.cpp'),'-o',str(directory / 'test')],check=True)
    subprocess.run([str(directory / 'test')],check=True)
print('Home reader shortcuts keep input ownership and dispatch guards')
