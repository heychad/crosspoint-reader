"""Exercise the production embedded-edge hook across load and settings resave."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/CrossPointSettings.cpp').read_text()
start = source.index('  forEachBaseSetting(visit);', source.index('bool CrossPointSettings::fromJson('))
start = source.index('  if (BoardConfig::hasTouch()) {', start)
end = source.index('  needsResave |= readergestures::readBindings', start)
hook = source[start:end]
code = r'''
#include <ArduinoJson.h>
#include <cassert>
#include <cstdint>
#include "personalization/ReaderGestureJson.h"
namespace BoardConfig {bool touch=true;bool hasTouch(){return touch;}}
struct Settings {
 uint8_t readerLeftEdgeUp=0, readerLeftEdgeDown=0, readerRightEdgeUp=0, readerRightEdgeDown=0;
 void load(JsonVariantConst doc) {
  auto visit=[&] {
   readerLeftEdgeUp=doc["readerLeftEdgeUp"] | readerLeftEdgeUp;
   readerLeftEdgeDown=doc["readerLeftEdgeDown"] | readerLeftEdgeDown;
   readerRightEdgeUp=doc["readerRightEdgeUp"] | readerRightEdgeUp;
   readerRightEdgeDown=doc["readerRightEdgeDown"] | readerRightEdgeDown;
  };
  visit();
''' + hook + r'''
 }
};
int main(){
 JsonDocument old;old["leftEdgeUp"]=1;old["rightEdgeDown"]=6;
 Settings first;first.load(old.as<JsonVariantConst>());
 assert(first.readerLeftEdgeUp==static_cast<uint8_t>(readershortcuts::Action::Brighter));
 assert(first.readerRightEdgeDown==static_cast<uint8_t>(readershortcuts::Action::PreviousChapter));
 // An ordinary settings save writes the loaded values under modern names.
 JsonDocument saved;saved["readerLeftEdgeUp"]=first.readerLeftEdgeUp;
 saved["readerRightEdgeDown"]=first.readerRightEdgeDown;saved["leftEdgeUp"]=2;
 Settings reboot;reboot.load(saved.as<JsonVariantConst>());
 assert(reboot.readerLeftEdgeUp==first.readerLeftEdgeUp && reboot.readerRightEdgeDown==first.readerRightEdgeDown);
 BoardConfig::touch=false;Settings unsupported;unsupported.load(old.as<JsonVariantConst>());
 assert(unsupported.readerLeftEdgeUp==0 && unsupported.readerRightEdgeDown==0);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp)
    (tmp/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20','-I',str(root/'src'),'-I',sys.argv[1],str(tmp/'test.cpp'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('Embedded settings hook preserves modern values across reload')
