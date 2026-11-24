"""Exercise the production side-shortcut input method with physical button frames."""
import os
from pathlib import Path
import shlex
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
  uint8_t powerUpChordAction=uint8_t(Action::Default);
} SETTINGS;
enum class QuickLockTrigger { None, SideUpShort, SideUpLong, SideDownShort, SideDownLong, UpDown };
uint32_t clockMs=0;
uint32_t millis(){return clockMs;}
struct MappedInputManager {
  enum class Button { Up, Down, Power };
  std::array<bool,3> down{}, pressed{}, released{}, consumed{};
  SideButtonContactGuard guard;
  bool blocked(Button b) const {return b!=Button::Power && guard.blocks(b==Button::Up);}
  bool isPressed(Button b) const {return !blocked(b) && down[static_cast<int>(b)];}
  bool wasPressed(Button b) const {return !blocked(b) && pressed[static_cast<int>(b)];}
  bool wasReleased(Button b) const {return !blocked(b) && released[static_cast<int>(b)];}
  bool isNavDirectionSwapped() const{return false;}
  void consumeSideButtonUntilReleased(Button b){
    consumed[static_cast<int>(b)]=true;
    guard.consume(b==Button::Up && (down[0] || released[0]), b==Button::Down && (down[1] || released[1]));
  }
  void frame(bool u, bool d, bool upEdge, bool downEdge, bool upRelease, bool downRelease) {
    down={u,d,false}; pressed={upEdge,downEdge,false}; released={upRelease,downRelease,false};
    guard.update(u,d,upRelease,downRelease);
  }
};
class EpubReaderActivity {
 public:
  readershortcuts::Tracker shortcutTracker;
  bool shortcutSideSwapped=false;
  MappedInputManager mappedInput;
  int actions=0; Action last=Action::Default; QuickLockTrigger origin=QuickLockTrigger::None;
  int pendingManualTurn=0;
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

  SETTINGS.readerSideFollowOrientation=1;
  SETTINGS.readerUpTap=uint8_t(Action::NextPage);
  SETTINGS.readerDownTap=uint8_t(Action::PreviousPage);
  SETTINGS.readerUpHold=SETTINGS.readerDownHold=uint8_t(Action::Disabled);
  SETTINGS.readerSideChord=uint8_t(Action::Disabled);
  for (int orientation=0; orientation<4; ++orientation) {
    SETTINGS.orientation=orientation;
    EpubReaderActivity fast;
    fast.mappedInput.frame(true,false,true,false,false,false);
    assert(fast.handleReaderShortcutInput());
    assert(fast.last==(orientation==0?Action::NextPage:Action::PreviousPage));
    assert(fast.origin==QuickLockTrigger::SideUpShort && fast.mappedInput.consumed[0]);
    fast.mappedInput.frame(true,false,false,false,false,false);
    assert(!fast.handleReaderShortcutInput());
    fast.mappedInput.frame(false,false,false,false,true,false);
    assert(!fast.mappedInput.wasReleased(MappedInputManager::Button::Up));
    assert(!fast.handleReaderShortcutInput() && fast.actions==1);
  }
  SETTINGS.orientation=0;
  {
    SETTINGS.powerUpChordAction=uint8_t(Action::QuickLock);
    EpubReaderActivity deferred;
    deferred.mappedInput.frame(true,false,true,false,false,false);
    assert(!deferred.handleReaderShortcutInput());
    deferred.mappedInput.frame(false,false,false,false,true,false);
    assert(deferred.handleReaderShortcutInput() && deferred.last==Action::NextPage);
    assert(!deferred.mappedInput.consumed[0]);
    SETTINGS.powerUpChordAction=uint8_t(Action::Default);
  }
  {
    SETTINGS.readerSideChord=uint8_t(Action::Screenshot);
    EpubReaderActivity chord;
    chord.mappedInput.frame(true,false,true,false,false,false);
    assert(!chord.handleReaderShortcutInput());
    chord.mappedInput.frame(true,true,false,true,false,false);
    assert(chord.handleReaderShortcutInput() && chord.last==Action::Screenshot);
    assert(chord.actions==1);
    SETTINGS.readerSideChord=uint8_t(Action::Disabled);
  }
  {
    SETTINGS.readerUpHold=uint8_t(Action::Bookmark);
    EpubReaderActivity held;
    clockMs=0;
    held.mappedInput.frame(true,false,true,false,false,false);
    assert(!held.handleReaderShortcutInput());
    clockMs=readershortcuts::Tracker::HOLD_MS;
    held.mappedInput.frame(true,false,false,false,false,false);
    assert(held.handleReaderShortcutInput() && held.last==Action::Bookmark);
    assert(held.actions==1);
    SETTINGS.readerUpHold=uint8_t(Action::Disabled);
  }
  {
    EpubReaderActivity overlapping;
    overlapping.mappedInput.frame(false,true,false,true,false,false);
    assert(!overlapping.handleReaderShortcutInput());
    overlapping.mappedInput.frame(true,false,true,false,false,true);
    assert(overlapping.handleReaderShortcutInput() && overlapping.last==Action::PreviousPage);
    overlapping.mappedInput.frame(false,false,false,false,true,false);
    assert(overlapping.handleReaderShortcutInput() && overlapping.last==Action::NextPage);
    assert(overlapping.actions==2);
  }
  {
    EpubReaderActivity rapid;
    rapid.pendingManualTurn=1;  // First turn is still waiting behind the reader guard.
    rapid.mappedInput.frame(true,false,true,false,false,false);
    assert(!rapid.handleReaderShortcutInput());
    assert(!rapid.mappedInput.consumed[0] && rapid.pendingManualTurn==1);
    rapid.pendingManualTurn=0;  // First turn has now executed.
    rapid.mappedInput.frame(false,false,false,false,true,false);
    assert(rapid.handleReaderShortcutInput() && rapid.last==Action::NextPage);
    assert(rapid.actions==1);
  }
  {
    SETTINGS.readerUpTap=uint8_t(Action::Default);
    EpubReaderActivity native;
    native.mappedInput.frame(true,false,true,false,false,false);
    assert(native.handleReaderShortcutInput() && native.last==Action::PreviousPage);
  }
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(fixture)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', *shlex.split(os.environ.get('CXXFLAGS', '')),
                    '-I', str(ROOT / 'src'), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Oriented side shortcuts passed')
