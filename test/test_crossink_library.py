"""Check exact library-policy migration without touching source or unrelated settings."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PROGRAM = r'''
#include <cassert>
#include <string>
#include "personalization/CrossInkLibraryImport.h"

std::string json(const JsonDocument& document) {
  std::string value;
  serializeJson(document, value);
  return value;
}

int main() {
  constexpr unsigned nativeTabs[] = {7, 1, 2, 5, 6, 3, 4};
  for (unsigned method = 0; method < 256; ++method) {
    for (unsigned direction = 0; direction < 2; ++direction) {
      const bool supported = method < 7 && nativeTabs[method] != 255;
      JsonDocument legacy, current;
      legacy["librarySortMethod"] = method;
      legacy["librarySortDescending"] = direction;
      const auto source = json(legacy);
      current.to<JsonObject>();
      assert(crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, false) == supported);
      if (!supported) {
        assert(current.size() == 0);
        continue;
      }
      const unsigned tab = nativeTabs[method];
      assert(current["libraryViewTab"] == tab);
      assert(current["libraryViewDescendingTabs"] == ((193 & ~(1u << tab)) | (direction << tab)));
      assert(current["librarySortMethod"].isUnbound() && current["librarySortDescending"].isUnbound());
      assert(!crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, true));
      for (unsigned bits = 0; bits < 256; ++bits) {
        current["libraryViewTab"] = 0;
        current["libraryViewDescendingTabs"] = bits;
        current["unrelated"] = "preserved";
        const auto original = json(current);
        assert(!crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, false));
        assert(json(current) == original);
        assert(crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, true));
        assert(current["libraryViewTab"] == tab);
        assert(current["libraryViewDescendingTabs"] == ((bits & ~(1u << tab)) | (direction << tab)));
        assert(current["unrelated"] == "preserved" && json(legacy) == source);
      }
      for (const char* key : {"libraryViewTab", "libraryViewDescendingTabs"}) {
        current.clear();
        current[key] = nullptr;
        const auto original = json(current);
        assert(!crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, false));
        assert(json(current) == original);
        assert(crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, true));
        assert(current["libraryViewTab"] == tab);
        assert(current["libraryViewDescendingTabs"] == ((193 & ~(1u << tab)) | (direction << tab)));
      }
    }
  }
  for (const char* key : {"librarySortMethod", "librarySortDescending"}) {
    for (const char* invalid : {"-1", "256", "1.0", "true", "null", "\"1\"", "[]", "{}"}) {
      JsonDocument legacy, current;
      legacy["librarySortMethod"] = 1;
      legacy["librarySortDescending"] = 1;
      JsonDocument bad;
      assert(!deserializeJson(bad, invalid));
      legacy[key] = bad.as<JsonVariantConst>();
      current["libraryViewTab"] = 4;
      current["libraryViewDescendingTabs"] = 31;
      const auto original = json(current);
      assert(!crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, true));
      assert(json(current) == original);
    }
    JsonDocument legacy, current;
    legacy["librarySortMethod"] = 1;
    legacy["librarySortDescending"] = 1;
    legacy.remove(key);
    current.to<JsonObject>();
    assert(!crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, true));
    assert(current.size() == 0);
  }
  for (int invalid : {2, 255}) {
    JsonDocument legacy, current;
    legacy["librarySortMethod"] = 1;
    legacy["librarySortDescending"] = invalid;
    current.to<JsonObject>();
    assert(!crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, true));
  }
  constexpr const char* keys[] = {
    "libraryUseMetadata", "showHiddenFiles", "removeReadBooksFromRecents", "moveFinishedToReadFolder",
    "libraryShowSeries", "libraryShowGenre", "libraryShowEpub", "libraryShowXtc", "libraryShowTxt",
    "libraryShowMarkdown", "libraryHideFinishedBooks", "hideFileExtension", "fileBrowserDisplay", "recentBooksView",
    "libraryListExpanded"
  };
  for (const char* key : keys) {
    for (int value : {0, 1}) {
      JsonDocument legacy, current;
      legacy[key] = value;
      current.to<JsonObject>();
      const auto source = json(legacy);
      assert(crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, false));
      assert(current[key].is<uint8_t>() && current[key].as<int>() == value);
      assert(!crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, true));
      current[key] = 1 - value;
      current["pluginsEnabled"] = 1;
      current["unrelated"] = "preserved";
      const auto original = json(current);
      assert(!crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, false));
      assert(json(current) == original);
      assert(crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, true));
      assert(current[key].as<int>() == value);
      assert(current["pluginsEnabled"] == 1 && current["unrelated"] == "preserved");
      assert(json(legacy) == source);
    }
    for (const char* invalid : {"-1", "2", "256", "1.0", "true", "null", "\"1\"", "[]", "{}"}) {
      JsonDocument legacy, current;
      const std::string source = std::string("{\"") + key + "\":" + invalid + "}";
      assert(!deserializeJson(legacy, source));
      current[key] = 0;
      const auto original = json(current);
      assert(!crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, true));
      assert(json(current) == original);
    }
  }
  for (int rows : {0,1}) {
    JsonDocument legacy, current;
    legacy["fileBrowserPreview"] = rows;
    current.to<JsonObject>();
    const auto source = json(legacy);
    assert(crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(),current,false));
    assert(current["fileBrowserDisplay"] == rows && current["fileBrowserPreview"].isUnbound());
    assert(!crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(),current,true));
    current["fileBrowserDisplay"] = nullptr;
    assert(!crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(),current,false));
    assert(current["fileBrowserDisplay"].isNull());
    assert(crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(),current,true));
    assert(current["fileBrowserDisplay"] == rows && json(legacy) == source);
    legacy["fileBrowserDisplay"] = 1 - rows;
    assert(crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(),current,true));
    assert(current["fileBrowserDisplay"] == 1 - rows);
    for (const char* invalid : {"-1","2","256","1.0","true","null","\"1\"","[]","{}"}) {
      JsonDocument malformed;
      assert(!deserializeJson(malformed,std::string("{\"fileBrowserDisplay\":")+invalid+",\"fileBrowserPreview\":1}"));
      const auto original = json(current);
      assert(!crossinkimport::stageLibrary(malformed.as<JsonVariantConst>(),current,true));
      assert(json(current) == original);
      malformed.remove("fileBrowserDisplay");
      assert(!deserializeJson(malformed,std::string("{\"fileBrowserPreview\":")+invalid+"}"));
      assert(!crossinkimport::stageLibrary(malformed.as<JsonVariantConst>(),current,true));
      assert(json(current) == original);
    }
  }
  JsonDocument legacy, current;
  legacy.to<JsonObject>();
  current.to<JsonObject>();
  for (const char* key : keys) legacy[key] = 1;
  legacy["fontFamily"] = 1;
  legacy["dictionaryFont"] = "Later";
  legacy["nearbyReceiveFolder"] = "/ignored";
  const auto source = json(legacy);
  assert(crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, true));
  assert(current.size() == sizeof(keys) / sizeof(keys[0]));
  for (const char* key : keys) assert(current[key] == 1);
  assert(json(legacy) == source);
  assert(!crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), current, false));
  JsonDocument array;
  array.to<JsonArray>();
  const auto original = json(current);
  assert(!crossinkimport::stageLibrary(array.as<JsonVariantConst>(), current, true));
  assert(json(current) == original);
  assert(!crossinkimport::stageLibrary(legacy.as<JsonVariantConst>(), array, true));
  assert(array.is<JsonArray>() && array.size() == 0);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    source = directory / 'test.cpp'
    binary = directory / 'test'
    source.write_text(PROGRAM)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17',
                    *shlex.split(os.environ.get('CXXFLAGS', '')),
                    '-I', str(ROOT / 'src'),
                    '-I', str(ROOT / '.pio/libdeps/x4pro/ArduinoJson/src'),
                    str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Library imports preserve source, ownership, unrelated settings, and reject invalid policy values')
