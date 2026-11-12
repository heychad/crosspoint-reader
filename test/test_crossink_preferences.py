"""Check preference import with and without experimental automatic backups."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
program = r'''
#include <cassert>
#include <string>
#include <tuple>
#include <ArduinoJson.h>
#include "personalization/CrossInkPreferencesImport.h"
int main() {
  for (const auto& name : {std::string("Literata"), std::string("Font Name"), std::string("caf\xc3\xa9"), std::string(31, 'a')}) {
    JsonDocument old, next;
    old["sdFontFamilyName"] = name;
    old["fontSize"] = 21;
    next.to<JsonObject>();
    assert(crossinkimport::stagePreferences(old.as<JsonVariantConst>(), next, false));
    assert(next["sdFontFamilyName"] == name && next["fontSize"] == 21);
    assert(!crossinkimport::stagePreferences(old.as<JsonVariantConst>(), next, false));
    next["fontSize"] = 18;
    assert(!crossinkimport::stagePreferences(old.as<JsonVariantConst>(), next, false));
    assert(next["fontSize"] == 18);
    assert(crossinkimport::stagePreferences(old.as<JsonVariantConst>(), next, true));
    assert(next["fontSize"] == 21);
  }
  for (const auto& name : {std::string(""), std::string(32, 'a'), std::string(".hidden"), std::string("bad/name"),
                           std::string("bad\\name"), std::string("bad\nname"), std::string("Font\0Other", 10),
                           std::string("\xc0\xaf"), std::string("\xed\xa0\x80"), std::string("\xe2\x82")}) {
    JsonDocument old, next;
    old["sdFontFamilyName"] = name;
    old["fontSize"] = 21;
    next.to<JsonObject>();
    assert(!crossinkimport::stagePreferences(old.as<JsonVariantConst>(), next, true));
    assert(next["sdFontFamilyName"].isUnbound() && next["fontSize"].isUnbound());
  }
  for (const char* target : {R"({"sdFontFamilyName":"Other","fontSize":18})",
                             R"({"fontFamily":2,"fontSize":18})",
                             R"({"sdFontFamilyName":"","fontSize":18})"}) {
    JsonDocument old, next;
    old["sdFontFamilyName"] = "Literata";
    old["fontSize"] = 21;
    assert(!deserializeJson(next, target));
    assert(!crossinkimport::stagePreferences(old.as<JsonVariantConst>(), next, false));
    assert(next["fontSize"] == 18);
    assert(crossinkimport::stagePreferences(old.as<JsonVariantConst>(), next, true));
    assert(next["sdFontFamilyName"] == "Literata" && next["fontSize"] == 21);
  }
  for (int size : {0, 3, 7, 256}) {
    JsonDocument old, next;
    old["sdFontFamilyName"] = "Literata";
    old["fontSize"] = size;
    next.to<JsonObject>();
    assert(crossinkimport::stagePreferences(old.as<JsonVariantConst>(), next, false));
    assert(next["sdFontFamilyName"] == "Literata" && next["fontSize"].isUnbound());
  }
  for (const auto& field : {std::tuple{"refreshFrequency",0,5}, {"screenInverted",0,1},
                            {"hideBatteryPercentage",0,2}, {"sleepTimeoutMinutes",1,31},
                            {"indexingMethod",0,1}, {"fadingFix",0,1},
                            {"pwrBtnFootnoteBack",0,1}, {"frontlightRestoreOnWake",0,1}}) {
    const auto [key, low, high] = field;
    for (int value : {low,high}) {
      JsonDocument old,next; old[key]=value;next.to<JsonObject>();
      assert(crossinkimport::stagePreferences(old.as<JsonVariantConst>(),next,false));
      assert(next[key]==value);
      next[key]=low;old[key]=high;
      assert(!crossinkimport::stagePreferences(old.as<JsonVariantConst>(),next,false));
      assert(next[key]==low);
      assert(crossinkimport::stagePreferences(old.as<JsonVariantConst>(),next,true));
      assert(next[key]==high);
    }
    for(int value : {low-1,high+1}) {
      JsonDocument old,next;old[key]=value;next.to<JsonObject>();
      assert(!crossinkimport::stagePreferences(old.as<JsonVariantConst>(),next,true));
      assert(next[key].isUnbound());
    }
    JsonDocument old,next;old[key]=true;next.to<JsonObject>();
    assert(!crossinkimport::stagePreferences(old.as<JsonVariantConst>(),next,true));
  }
  JsonDocument source, target;
  assert(!deserializeJson(source, R"({"uiScale":1,"trackReadingStats":0,"readingIdleTimeThresholdUnits":60,"autoBackupStats":1})"));
  target.to<JsonObject>();
  assert(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), target, true));
  assert(target["uiScale"] == 1 && target["trackReadingStats"] == 0);
  assert(target["readingIdleTimeThresholdUnits"] == 60);
#ifdef CROSSPOINT_EXPERIMENTAL_STATS_BACKUP
  assert(target["autoBackupStats"] == 1);
#else
  assert(target["autoBackupStats"].isUnbound());
#endif
  source["uiScale"] = true; source["trackReadingStats"] = 3; source["readingIdleTimeThresholdUnits"] = 61;
  source.remove("autoBackupStats");
  assert(!crossinkimport::stagePreferences(source.as<JsonVariantConst>(), target, true));
  assert(target["uiScale"] == 1 && target["readingIdleTimeThresholdUnits"] == 60);
  for (const auto& pair : {std::pair{0,0}, {1,1}, {2,2}, {3,3}, {4,5}, {5,4}, {7,9}, {8,12}, {9,6}, {10,12}, {11,13}}) {
    JsonDocument old, next;
    old["sleepScreen"] = pair.first;
    next.to<JsonObject>();
    assert(crossinkimport::stagePreferences(old.as<JsonVariantConst>(), next, false));
    assert(next["sleepScreen"] == pair.second);
    assert(!crossinkimport::stagePreferences(old.as<JsonVariantConst>(), next, false));
  }
  for (int unsupported : {255}) {
    JsonDocument old, next;
    old["sleepScreen"] = unsupported;
    next.to<JsonObject>();
    assert(!crossinkimport::stagePreferences(old.as<JsonVariantConst>(), next, true));
    assert(next["sleepScreen"].isUnbound());
  }
  {
    JsonDocument old, next;
    for (const auto pair : {std::pair{6,14},std::pair{12,15}}) {
    old["sleepScreen"] = pair.first;
    next.clear();next.to<JsonObject>();
#ifdef CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY
    assert(crossinkimport::stagePreferences(old.as<JsonVariantConst>(), next, true));
    assert(next["sleepScreen"] == pair.second);
#else
    assert(!crossinkimport::stagePreferences(old.as<JsonVariantConst>(), next, true));
    assert(next["sleepScreen"].isUnbound());
#endif
    }
  }
  JsonDocument sleepSource, sleepTarget;
  assert(!deserializeJson(sleepSource, R"({"sleepScreen":4,"sleepScreenCoverMode":1,"sleepScreenCoverFilter":2,"quickResumeSleepScreen":1})"));
  assert(!deserializeJson(sleepTarget, R"({"sleepScreen":1,"sleepScreenCoverMode":0})"));
  assert(crossinkimport::stagePreferences(sleepSource.as<JsonVariantConst>(), sleepTarget, false));
  assert(sleepTarget["sleepScreen"] == 1 && sleepTarget["sleepScreenCoverMode"] == 0);
  assert(sleepTarget["sleepScreenCoverFilter"] == 2 && sleepTarget["quickResumeSleepScreen"] == 1);
  assert(crossinkimport::stagePreferences(sleepSource.as<JsonVariantConst>(), sleepTarget, true));
  assert(sleepTarget["sleepScreen"] == 5 && sleepTarget["sleepScreenCoverMode"] == 1);
  sleepSource["sleepScreen"] = 255;
  sleepSource["sleepScreenCoverMode"] = 2;
  sleepSource["sleepScreenCoverFilter"] = 3;
  sleepSource["quickResumeSleepScreen"] = true;
  assert(!crossinkimport::stagePreferences(sleepSource.as<JsonVariantConst>(), sleepTarget, true));
  assert(sleepTarget["sleepScreen"] == 5 && sleepTarget["sleepScreenCoverMode"] == 1);
  assert(sleepTarget["sleepScreenCoverFilter"] == 2 && sleepTarget["quickResumeSleepScreen"] == 1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    for flag in ([], ['-DCROSSPOINT_EXPERIMENTAL_STATS_BACKUP=1'], ['-DCROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY=1']):
        subprocess.run(['c++', '-std=c++20', *flag, '-I', str(root / 'src'),
                        '-I', str(root / '.pio/libdeps/x4pro/ArduinoJson/src'),
                        str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
print('CrossInk preference values and backup build gate passed')
