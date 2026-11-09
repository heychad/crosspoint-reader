"""Exercise the production light transaction with a controlled HAL and settings store."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/reader/ReaderGestureInput.cpp').read_text()
method = 'void ReaderActivity::applyLightGesture(' + source.split('void ReaderActivity::applyLightGesture(', 1)[1].split('#endif', 1)[0]
code = r'''
#include <algorithm>
#include <cassert>
#include "personalization/ReaderGestures.h"
using readershortcuts::Action;
struct Light {
  bool available=true, temperature=true, on=false;
  uint8_t level=60, warm=50;
  bool present() const { return available; }
  bool hasColorTemperature() const { return temperature; }
  bool isOn() const { return on; }
  uint8_t brightness() const { return level; }
  uint8_t warmth() const { return warm; }
  void setOn(bool value) { on=value; }
  void setBrightness(uint8_t value) { level=value; }
  void setWarmth(uint8_t value) { warm=value; }
} Frontlight;
struct Settings {
  uint8_t frontlightBrightness=60, frontlightWarmth=50;
  bool frontlightOn=false;
  int saves=0;
  void saveToFile() { ++saves; }
} SETTINGS;
class ReaderActivity {
 public:
  struct LightGesture { bool active=false, initialOn=false; uint8_t initialValue=0; Action action=Action::Default; } lightGesture;
  void applyLightGesture(const readergestures::Event&);
};
''' + method + r'''
int main() {
  using E=readergestures::EventKind;
  ReaderActivity reader;
  reader.applyLightGesture({E::LightBegin,Action::Brighter,20});
  assert(Frontlight.level==80 && Frontlight.on && SETTINGS.saves==0);
  reader.applyLightGesture({E::LightUpdate,Action::Brighter,70});
  assert(Frontlight.level==100 && SETTINGS.saves==0);
  reader.applyLightGesture({E::LightUpdate,Action::Brighter,10});
  assert(Frontlight.level==70 && SETTINGS.saves==0);
  reader.applyLightGesture({E::LightFinish,Action::Brighter});
  assert(SETTINGS.saves==1);
  reader.applyLightGesture({E::LightFinish,Action::Brighter});
  assert(SETTINGS.saves==1);
  Frontlight.on=SETTINGS.frontlightOn=false;
  reader.applyLightGesture({E::LightBegin,Action::Brighter,20});
  reader.applyLightGesture({E::LightCancel,Action::Brighter});
  assert(Frontlight.level==70 && !Frontlight.on && SETTINGS.saves==1);
  reader.applyLightGesture({E::LightBegin,Action::Warmer,20});
  assert(Frontlight.warm==70 && !Frontlight.on);
  reader.applyLightGesture({E::LightFinish,Action::Warmer});
  assert(SETTINGS.saves==2);
  Frontlight.temperature=false;
  reader.applyLightGesture({E::LightBegin,Action::Cooler,30});
  reader.applyLightGesture({E::LightFinish,Action::Cooler});
  assert(Frontlight.warm==70 && SETTINGS.saves==2);
  Frontlight.available=false;
  reader.applyLightGesture({E::LightBegin,Action::Dimmer,90});
  reader.applyLightGesture({E::LightFinish,Action::Dimmer});
  assert(Frontlight.level==70 && SETTINGS.saves==2);
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory)
    (p/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20','-I',str(root/'src'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('Reader light transaction checks passed')
