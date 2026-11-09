"""Run with python3 test/test_remote_button_input.py (requires a host C++ compiler)."""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/MappedInputManager.cpp").read_text()
# Exercise production frame/dispatch methods with physical input under test control.
methods = []
for signature, following in (
    ("void MappedInputManager::update(", "bool MappedInputManager::isNavDirectionSwapped("),
    ("bool MappedInputManager::mapButton(", "namespace {"),
    ("bool MappedInputManager::injectRemoteClick(", "bool MappedInputManager::wasLongPressed("),
    ("bool MappedInputManager::isPressed(", "MappedInputManager::Labels MappedInputManager::mapLabels("),
):
    methods.append(signature + source.split(signature, 1)[1].split(following, 1)[0])

stub = r"""
#pragma once
#include <cstdint>
struct HalGPIO {
  struct TouchSnapshot { static constexpr unsigned MAX_CONTACTS = 4; };
  void suppressTouchContact() {}

  static constexpr uint8_t BTN_BACK = 0, BTN_CONFIRM = 1, BTN_LEFT = 2, BTN_RIGHT = 3, BTN_UP = 4, BTN_DOWN = 5, BTN_POWER = 6;
  bool pressed = false, released = false, held = false;
  bool homePresent = false, homeDown = false, homeTap = false, homeLong = false, homePress = false;
  uint8_t button = 0, additionalHeld = 0;
  void update() {}
  bool wasAnyPressed() const { return pressed; }
  bool wasAnyReleased() const { return released; }
  bool wasPressed(uint8_t b) const { return b == button && pressed; }
  bool wasReleased(uint8_t b) const { return b == button && released; }
  bool isPressed(uint8_t b) const { return (b == button && held) || (additionalHeld & (1u << b)); }
  unsigned long getHeldTime() const { return 900; }
  bool wasCapacitivePagePressed() const { return false; }
  bool hasHomeKey() const { return homePresent; }
  bool isHomeKeyDown() const { return homeDown; }
  bool wasHomeKeyTapped() const { return homeTap; }
  bool wasHomeKeyLongPressed() const { return homeLong; }
  bool wasHomeKeyPressed() const { return homePress; }
};
"""
harness = r"""
#include <cassert>
#include "MappedInputManager.h"
class GfxRenderer {};
unsigned long millis() { return 1000; }
constexpr unsigned long TOUCH_HELD_OVERRIDE_WINDOW_MS = 250;
struct CrossPointSettings {
  enum { VIBRATION_OFF, VIBRATION_TOUCH_PAGE };
  enum { IGNORE = 0, PWR_BOOKMARK = 7, PWR_SYNC = 8, PWR_READER_MENU = 9, PWR_QUICK_ACTIONS = 10 };
  enum { PREV_NEXT = 0, NEXT_PREV = 1, SIDE_BUTTONS_DISABLED = 2, NEXT_NEXT = 3, PREV_PREV = 4 };
  int sideButtonLayout = PREV_NEXT;
  uint8_t frontButtonBack = 0, frontButtonConfirm = 1, frontButtonLeft = 2, frontButtonRight = 3;
  int shortPwrBtn = IGNORE;
  int vibration = VIBRATION_OFF, hapticIntensity = 0;
  int homeButtonTapAction = 0, homeButtonDoubleTapAction = 0, homeButtonLongPressAction = 0;
} SETTINGS;
struct HalHaptics {
  static void feedback(bool, bool, int) {}
  static void longPress(bool, int) {}
};
bool MappedInputManager::isNavDirectionSwapped() const { return false; }
MappedInputManager::Button MappedInputManager::mapScreenDirection(Button b) const {
  switch (b) {
    case Button::ScreenLeft: return Button::Left;
    case Button::ScreenRight: return Button::Right;
    case Button::ScreenUp: return Button::Up;
    case Button::ScreenDown: return Button::Down;
    default: return b;
  }
}
bool MappedInputManager::wasBackGesture() const { return false; }
MappedInputManager::SwipeDir MappedInputManager::wasSwipe(int*, int*) const { return SwipeDir::None; }
""" + "\n".join(methods) + r"""
int main() {
  HalGPIO gpio;
  GfxRenderer renderer;
  MappedInputManager input(gpio, renderer);
  // A held contact that started in a menu cannot leak its long-press or
  // release into the page, and a fresh contact must work after release.
  gpio.homePresent = true;
  SETTINGS.homeButtonTapAction = static_cast<int>(HomeButtonAction::Library);
  SETTINGS.homeButtonDoubleTapAction = static_cast<int>(HomeButtonAction::Ignore);
  SETTINGS.homeButtonLongPressAction = static_cast<int>(HomeButtonAction::FileTransfer);
  gpio.homeDown = gpio.homePress = true;
  input.update();
  input.resetHomeButtonInput();
  gpio.homePress = false; gpio.homeLong = true;
  input.update();
  assert(input.homeButtonAction() == HomeButtonAction::Ignore);
  gpio.homeLong = false; gpio.homeDown = false; gpio.homeTap = true;
  input.update();
  assert(input.homeButtonAction() == HomeButtonAction::Ignore);
  gpio.homeTap = false; gpio.homeDown = gpio.homePress = true;
  input.update();
  gpio.homePress = false; gpio.homeDown = false; gpio.homeTap = true;
  input.update();
  assert(input.homeButtonAction() == HomeButtonAction::Library);
  input.resetHomeButtonInput();
  assert(input.homeButtonAction() == HomeButtonAction::Ignore);
  gpio.homeTap = false; gpio.homeDown = gpio.homePress = true;
  input.update();
  gpio.homePress = false; gpio.homeLong = true;
  input.update();
  assert(input.homeButtonAction() == HomeButtonAction::FileTransfer);
  input.resetHomeButtonInput();
  gpio.homeLong = false; gpio.homeDown = false;
  input.update();
  assert(input.homeButtonAction() == HomeButtonAction::Ignore);
  gpio.homeTap = gpio.homePresent = false;
  using Button = MappedInputManager::Button;
  struct Mapping { RemoteButton remote; Button button; Button other; };
  for (const auto mapping : {
      Mapping{RemoteButton::Confirm, Button::Confirm, Button::Back},
      Mapping{RemoteButton::Back, Button::Back, Button::Confirm},
      Mapping{RemoteButton::Next, Button::NavNext, Button::NavPrevious},
      Mapping{RemoteButton::Next, Button::Right, Button::Left},
      Mapping{RemoteButton::Previous, Button::NavPrevious, Button::NavNext},
      Mapping{RemoteButton::Previous, Button::Left, Button::Right}}) {
    const auto remote = mapping.remote;
    const auto button = mapping.button;
    const auto other = mapping.other;
    input.update();
    assert(input.injectRemoteClick(remote));
    assert(input.hasRemoteClick());
    assert(input.wasPressed(button) && input.wasReleased(button));
    assert(!input.wasPressed(other) && !input.wasReleased(other));
    assert(!input.isPressed(button) && input.getHeldTime() == 0);
    assert(!input.injectRemoteClick(remote));
    input.update();
    assert(!input.hasRemoteClick());
    assert(!input.wasPressed(button) && !input.wasReleased(button));
    assert(input.getHeldTime() == 900);
  }
  SETTINGS.shortPwrBtn = CrossPointSettings::PWR_QUICK_ACTIONS;
  gpio.button = HalGPIO::BTN_POWER;
  gpio.released = true;
  input.update();
  assert(input.homeButtonAction() == HomeButtonAction::QuickActions);
  gpio.released = false;
  SETTINGS.shortPwrBtn = CrossPointSettings::IGNORE;
  input.update();
  // Physical edges and held Confirm/Back must win over a simultaneous remote click.
  gpio.pressed = true;
  assert(!input.injectRemoteClick(RemoteButton::Confirm));
  gpio.pressed = false;
  gpio.released = true;
  assert(!input.injectRemoteClick(RemoteButton::Back));
  gpio.released = false;
  gpio.held = true;
  for (Button button : {Button::Confirm, Button::Back}) {
    gpio.button = static_cast<uint8_t>(button);
    assert(!input.injectRemoteClick(RemoteButton::Confirm));
    assert(!input.injectRemoteClick(RemoteButton::Back));
  }
  gpio.held = false;
  assert(input.injectRemoteClick(RemoteButton::Back));
  input.update();
  gpio.button = HalGPIO::BTN_UP;
  gpio.pressed = gpio.held = true;
  input.update();
  input.consumeSideButtonUntilReleased(Button::Up);
  for (Button alias : {Button::Up, Button::PageBack, Button::NavPrevious}) {
    assert(!input.wasPressed(alias));
    assert(!input.isPressed(alias));
  }
  gpio.pressed = false;
  input.update();
  assert(!input.isPressed(Button::NavPrevious));
  gpio.held = false; gpio.released = true;
  input.update();
  assert(!input.wasReleased(Button::PageBack));
  assert(!input.wasReleased(Button::NavPrevious));
  gpio.released = false;
  input.update();
  gpio.pressed = gpio.held = true;
  input.update();
  assert(input.wasPressed(Button::NavPrevious));
  gpio.additionalHeld = 1u << HalGPIO::BTN_DOWN;
  input.consumeSideButtonUntilReleased(Button::Up);
  assert(!input.isPressed(Button::Up));
  assert(input.isPressed(Button::Down));
  assert(input.isPressed(Button::PageForward));
}
"""
manager_source = (ROOT / "src/activities/ActivityManager.cpp").read_text()
remote_turn = "bool ActivityManager::remoteTurn(" + manager_source.split(
    "bool ActivityManager::remoteTurn(", 1
)[1].split("bool ActivityManager::handleForcedRefresh()", 1)[0]
harness = harness.replace("int main() {", r"""
#include <atomic>
#include <memory>
#include "activities/reader/RemoteTurnGate.h"
struct RenderLock { static bool peek() { return false; } };
struct ReaderActivity {
  std::atomic<bool> pageRendered{true};
  bool menu = false;
  int pages = 0, chapters = 0, turnReports = 0;
  bool reportedForward = false, reportedSuccess = false;
  void notePageTurn(bool forward, bool succeeded) {
    ++turnReports;
    reportedForward = forward;
    reportedSuccess = succeeded;
  }
  bool inputOverPage() const { return menu; }
  bool pageTurn(bool forward) { pages += forward ? 1 : -1; return true; }
  bool skipPages(int step) { chapters += step; return true; }
};
struct ActivityManager {
  MappedInputManager& mappedInput;
  std::unique_ptr<ReaderActivity> currentActivity = std::make_unique<ReaderActivity>();
  bool context = true, foregroundReader = true, locked = false;
  bool isInputLocked() const { return locked; }
  bool isRemoteReaderContext() const { return context; }
  bool isForegroundReader() const { return foregroundReader; }
  bool remoteClick(RemoteButton button) { return mappedInput.injectRemoteClick(button); }
  void requestUpdate() {}
  bool remoteTurn(bool forward, bool chapter);
};
""" + remote_turn + r"""
void checkRouting(MappedInputManager& input) {
  ActivityManager manager{input};
  input.update();
  assert(manager.remoteTurn(true, false));
  assert(manager.currentActivity->pages == 1 && !input.hasRemoteClick());
  assert(manager.currentActivity->turnReports == 1);
  assert(manager.currentActivity->reportedForward && manager.currentActivity->reportedSuccess);
  for (bool reader : {true, false}) {
    manager.foregroundReader = reader;
    manager.currentActivity->menu = true;
    for (bool forward : {true, false}) {
      input.update();
      assert(manager.remoteTurn(forward, false));
      assert(input.wasPressed(forward ? MappedInputManager::Button::NavNext
                                     : MappedInputManager::Button::NavPrevious));
      assert(manager.currentActivity->pages == 1);
      assert(!manager.remoteTurn(forward, false));
    }
    input.update();
    assert(!manager.remoteTurn(true, true));
    assert(manager.currentActivity->chapters == 0 && !input.hasRemoteClick());
  }
  manager.locked = true;
  input.update();
  assert(!manager.remoteTurn(true, false));
  assert(!manager.remoteTurn(true, true));
  assert(!input.hasRemoteClick());
  manager.locked = false;
  manager.context = false;
  assert(!manager.remoteTurn(true, false));
}
int main() {
""")
harness = harness.replace("  using Button =", "  checkRouting(input);\n  using Button =")
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / "HalGPIO.h").write_text(stub)
    cpp = directory / "remote_input.cpp"
    executable = directory / "remote_input"
    cpp.write_text("#include <initializer_list>\n" + harness)
    subprocess.run([
        "c++", "-std=c++20", "-DFREEINK_CAP_TOUCH=0", "-I", str(directory),
        "-I", str(ROOT / "src"), str(cpp), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
print("Remote button input regression checks passed")
