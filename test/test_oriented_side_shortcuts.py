"""Exercise the production side-shortcut input method with physical button frames."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/activities/reader/EpubReaderShortcuts.cpp').read_text()
start = source.index('bool EpubReaderActivity::handleReaderShortcutInput()')
end = source.index('\nvoid EpubReaderActivity::onReaderGestureStarted()', start)
method = source[start:end]
fixture = r'''
#include <cassert>
#include <array>
#include <cstdint>
#include "personalization/ReaderShortcuts.h"
using readershortcuts::Action;
using readershortcuts::Trigger;
struct CrossPointSettings { enum { OFF=0, CHAPTER_SKIP=1, ORIENTATION_CHANGE=2, PORTRAIT=0 }; };
struct Settings {
  uint8_t readerSideFollowOrientation=1, orientation=0, sideButtonLayout=0, longPressButtonBehavior=0;
  uint8_t readerUpTap=uint8_t(Action::Bookmark), readerDownTap=uint8_t(Action::Dictionary);
  uint8_t readerUpHold=uint8_t(Action::Rotate), readerDownHold=uint8_t(Action::Refresh);
  uint8_t readerSideChord=uint8_t(Action::Screenshot);
} SETTINGS;
enum class QuickLockTrigger { None, SideUpShort, SideUpLong, SideDownShort, SideDownLong, UpDown };
uint32_t clockMs=0;
uint32_t millis(){return clockMs;}
struct MappedInputManager {
  enum class Button { Up, Down, Power };
  std::array<bool,3> down{}, pressed{}, released{}, consumed{};
  bool isPressed(Button b) const {return down[static_cast<int>(b)];}
  bool wasPressed(Button b) const {return pressed[static_cast<int>(b)];}
  bool wasReleased(Button b) const {return released[static_cast<int>(b)];}
  bool isNavDirectionSwapped() const{return false;}
  void consumeSideButtonUntilReleased(Button b){consumed[static_cast<int>(b)]=true;}
  void frame(bool u, bool d, bool upEdge, bool downEdge, bool upRelease, bool downRelease) {
    down={u,d,false}; pressed={upEdge,downEdge,false}; released={upRelease,downRelease,false};
  }
};
class EpubReaderActivity {
 public:
  readershortcuts::Tracker shortcutTracker;
  bool shortcutSideSwapped=false;
  MappedInputManager mappedInput;
  int actions=0; Action last=Action::Default; QuickLockTrigger origin=QuickLockTrigger::None;
  bool customSideShortcuts() const {return true;}
  bool handleReaderShortcut(Action action, QuickLockTrigger trigger){++actions;last=action;origin=trigger;return true;}
  bool handleReaderShortcutInput();
};
''' + method + r'''
int main(){
  for (int orientation=0; orientation<4; ++orientation) {
    SETTINGS.orientation=orientation; clockMs=0; EpubReaderActivity reader;
    reader.mappedInput.frame(true,false,true,false,false,false); assert(!reader.handleReaderShortcutInput());
    reader.mappedInput.frame(false,false,false,false,true,false); assert(reader.handleReaderShortcutInput());
    assert(reader.last==(orientation==0?Action::Bookmark:Action::Dictionary));
    assert(reader.origin==QuickLockTrigger::SideUpShort);
    reader.mappedInput.frame(true,false,true,false,false,false); clockMs=10; assert(!reader.handleReaderShortcutInput());
    reader.mappedInput.frame(true,false,false,false,false,false); clockMs=800; assert(reader.handleReaderShortcutInput());
    assert(reader.last==(orientation==0?Action::Rotate:Action::Refresh));
    assert(reader.origin==QuickLockTrigger::SideUpLong && reader.mappedInput.consumed[0]);
    reader.mappedInput.frame(false,false,false,false,true,false); assert(!reader.handleReaderShortcutInput());
  }
  SETTINGS.orientation=0; clockMs=0; EpubReaderActivity rotation;
  rotation.mappedInput.frame(true,false,true,false,false,false); assert(!rotation.handleReaderShortcutInput());
  SETTINGS.orientation=1; rotation.mappedInput.frame(true,false,false,false,false,false); assert(!rotation.handleReaderShortcutInput());
  rotation.mappedInput.frame(false,false,false,false,true,false); assert(!rotation.handleReaderShortcutInput());
  assert(rotation.actions==0);
  SETTINGS.orientation=1; EpubReaderActivity chord; clockMs=0;
  chord.mappedInput.frame(true,false,true,false,false,false); assert(!chord.handleReaderShortcutInput());
  chord.mappedInput.frame(true,true,false,true,false,false); assert(chord.handleReaderShortcutInput());
  assert(chord.last==Action::Screenshot && chord.origin==QuickLockTrigger::UpDown);
  assert(chord.mappedInput.consumed[0] && chord.mappedInput.consumed[1]);
  SETTINGS.readerSideFollowOrientation=0; EpubReaderActivity fixed;
  fixed.mappedInput.frame(true,false,true,false,false,false); assert(!fixed.handleReaderShortcutInput());
  fixed.mappedInput.frame(false,false,false,false,true,false); assert(fixed.handleReaderShortcutInput());
  assert(fixed.last==Action::Bookmark);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(fixture)
    subprocess.run(['c++', '-std=c++20', '-fsanitize=address', '-fno-omit-frame-pointer',
                    '-I', str(ROOT / 'src'), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Oriented side shortcuts passed with AddressSanitizer')
