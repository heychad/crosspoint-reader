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
#include "personalization/LegacyHomeButtonShortcuts.h"
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
 static_assert(static_cast<int>(H::CycleFontFamily)==26 && static_cast<int>(H::SelectChapter)==27);
 static_assert(static_cast<int>(Action::HomeReader)==42 && static_cast<int>(Action::SelectChapter)==43);
 H imported=H::Ignore;assert(readershortcuts::readLegacyHomeAction(38,imported) && imported==H::SelectChapter);
 struct Mapping {int legacy;H home;Action action;};
 for(const auto& entry : {Mapping{6,H::ToggleFocus,Action::ToggleFocus},
                         Mapping{12,H::CycleAutoTurn,Action::CycleAutoTurn},
                         Mapping{26,H::ToggleHomeButton,Action::ToggleHomeButton},
                         Mapping{36,H::HomeReader,Action::HomeReader},
                         Mapping{39,H::RotateHalfTurn,Action::RotateHalfTurn}}){
   assert(readershortcuts::readLegacyHomeAction(entry.legacy,imported) && imported==entry.home);
   assert(homereadershortcuts::actionFor(imported)==entry.action);
 }
 assert(readershortcuts::readLegacyHomeAction(37,imported) && imported==H::Home);

 assert(homereadershortcuts::actionFor(H::Home)==Action::Default);
 assert(homereadershortcuts::actionFor(H::QuickActions)==Action::Default);
 assert(homereadershortcuts::actionFor(H::FileBrowser)==Action::FileBrowser);
 assert(homereadershortcuts::actionFor(H::Library)==Action::Library);
 assert(homereadershortcuts::actionFor(H::SavedItems)==Action::SavedItems);
 assert(homereadershortcuts::actionFor(H::FileTransfer)==Action::FileTransfer);
 assert(homereadershortcuts::actionFor(H::ReadingStatistics)==Action::ReadingStatistics);
 assert(homereadershortcuts::actionFor(H::ToggleCompletion)==Action::ToggleCompletion);
 assert(homereadershortcuts::actionFor(H::CycleFontFamily)==Action::CycleFontFamily);
 assert(homereadershortcuts::actionFor(H::SelectChapter)==Action::SelectChapter);
 assert(homereadershortcuts::actionFor(H::PreviousPage)==Action::PreviousPage);
 assert(homereadershortcuts::actionFor(H::Screenshot)==Action::Screenshot);
 assert(homereadershortcuts::actionFor(H::ToggleNight)==Action::ToggleNight);
 assert(homereadershortcuts::actionFor(H::ToggleTouch)==Action::ToggleTouch);
 assert(homereadershortcuts::actionFor(H::ToggleTilt)==Action::ToggleTilt);
 assert(homereadershortcuts::actionFor(H::Sleep)==Action::Sleep);
 ReaderActivity r;assert(!r.handleHomeReaderShortcut());
 r.mappedInput.action=H::Library;assert(r.handleHomeReaderShortcut() && r.calls==0);
 r.pageRendered=true;r.overlay=true;assert(r.handleHomeReaderShortcut() && r.calls==0);
 r.overlay=false;r.supported=false;assert(r.handleHomeReaderShortcut() && r.calls==0);
 r.supported=true;assert(r.handleHomeReaderShortcut() && r.calls==1 && r.last==Action::Library);
 r.mappedInput.action=H::FileTransfer;assert(r.handleHomeReaderShortcut() && r.calls==2 && r.last==Action::FileTransfer);
 r.mappedInput.action=H::ToggleCompletion;assert(r.handleHomeReaderShortcut() && r.calls==3 && r.last==Action::ToggleCompletion);
 r.mappedInput.action=H::CycleFontFamily;assert(r.handleHomeReaderShortcut() && r.calls==4 && r.last==Action::CycleFontFamily);
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
for action in ('FileBrowser','Library','SavedItems','FileTransfer','ReadingStatistics','PreviousPage','Screenshot','ToggleNight','ToggleTouch','ToggleTilt','Sleep','ToggleCompletion','CycleFontFamily'):
    assert f'case HomeButtonAction::{action}:' in epub_loop[:epub_loop.index('if (automaticPageTurnActive)')]
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / 'test.cpp').write_text(program)
    subprocess.run(['c++','-std=c++20','-I',str(root / 'src'),str(directory / 'test.cpp'),'-o',str(directory / 'test')],check=True)
    subprocess.run([str(directory / 'test')],check=True)
print('Home reader shortcuts keep input ownership and dispatch guards')
