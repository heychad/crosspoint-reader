"""Check persisted theme IDs when experimental Carousel is enabled."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ARDUINO_JSON = ROOT / '.pio/libdeps/x4pro/ArduinoJson/src'
PROGRAM = r'''
#include <cassert>
#include "personalization/CrossInkLibraryImport.h"
int main() {
  JsonDocument legacy, current;
  legacy["uiTheme"] = 4;
  current["uiTheme"] = 1;
  assert(crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, true));
  assert(current["uiTheme"].as<int>() == 7);
  legacy["uiTheme"] = 7;
  assert(crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, true));
  assert(current["uiTheme"].as<int>() == 4);
  legacy["uiTheme"] = 4;
  assert(!crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, false));
  assert(current["uiTheme"].as<int>() == 4);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / 'test.cpp'
    binary = Path(tmp) / 'test'
    source.write_text(PROGRAM)
    subprocess.run(['c++', '-std=c++17', '-DCROSSPOINT_EXPERIMENTAL_CAROUSEL=1',
                    '-I', str(ROOT / 'src'), '-I', str(ARDUINO_JSON), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Carousel and Cover Grid IDs migrate independently')
