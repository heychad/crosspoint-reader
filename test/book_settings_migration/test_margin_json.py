"""Exercise the production global-save and web single-margin compatibility paths."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
settings = (root / 'src/CrossPointSettings.cpp').read_text()
web = (root / 'src/network/CrossPointWebServer.cpp').read_text()
start = settings.index('  const auto legacyMargin = doc["screenMargin"];')
load = settings[start:settings.index('  readershortcuts::readEmbeddedLegacyPower', start)]
start = web.index('  const auto legacyMargin = doc["screenMargin"];')
post = web[start:web.index('  for (const auto& s : settings)', start)]
alias = next(line for line in settings.splitlines() if 'doc["screenMargin"] = doc["screenMarginHorizontal"]' in line)
code = r'''
#include <ArduinoJson.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
struct CrossPointSettings {
 static constexpr uint8_t SCREEN_MARGIN_MIN=5,SCREEN_MARGIN_MAX=150;
 uint8_t screenMargin=11,screenMarginVertical=13;
 bool loadLegacy(JsonVariantConst doc){bool needsResave=false;
''' + load + r'''
 return needsResave;}
};
CrossPointSettings SETTINGS;
int postLegacy(JsonDocument& doc){int applied=0;
''' + post + r'''
 return applied;}
void saveAlias(JsonDocument&doc){
''' + alias + r'''
}
int main(){
 for(int h=0;h<2;++h)for(int v=0;v<2;++v)for(int margin:{0,4,5,7,40,41,150,151,255}){
  JsonDocument doc;doc["screenMargin"]=margin;
  if(h)doc["screenMarginHorizontal"]=7;
  if(v)doc["screenMarginVertical"]=150;
  CrossPointSettings s;
  // The generic settings visitor has already applied the explicit axes.
  if(h)s.screenMargin=7;if(v)s.screenMarginVertical=150;
  assert(s.loadLegacy(doc.as<JsonVariantConst>())==(!h||!v));
  assert(s.screenMargin==(h?7:std::clamp(margin,5,150)));
  assert(s.screenMarginVertical==(v?150:std::clamp(margin,5,150)));
  SETTINGS={};if(h)SETTINGS.screenMargin=7;if(v)SETTINGS.screenMarginVertical=150;
  const bool valid=margin>=5&&margin<=150;
  assert(postLegacy(doc)==valid);
  assert(SETTINGS.screenMargin==(h?7:valid?margin:11));
  assert(SETTINGS.screenMarginVertical==(v?150:valid?margin:13));
 }
 JsonDocument doc;doc["screenMarginHorizontal"]=7;doc["screenMarginVertical"]=150;
 saveAlias(doc);assert(doc["screenMargin"].as<int>()==7);
 CrossPointSettings s;s.screenMargin=7;s.screenMarginVertical=150;
 assert(!s.loadLegacy(doc.as<JsonVariantConst>()));
 for(const char*invalid:{"{}","{\"screenMargin\":\"15\"}","{\"screenMargin\":true}"}){
  doc.clear();assert(!deserializeJson(doc,invalid));s={};
  assert(!s.loadLegacy(doc.as<JsonVariantConst>()));assert(postLegacy(doc)==0);
  assert(s.screenMargin==11&&s.screenMarginVertical==13);
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-I', sys.argv[1], str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Global margin migration and web compatibility preserve explicit axes and avoid redundant resaves')
