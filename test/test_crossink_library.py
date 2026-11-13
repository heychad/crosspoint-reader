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
  constexpr const char* keys[] = {
    "libraryUseMetadata", "showHiddenFiles", "removeReadBooksFromRecents", "moveFinishedToReadFolder",
    "libraryShowSeries", "libraryShowGenre", "libraryShowEpub", "libraryShowXtc", "libraryShowTxt",
    "libraryShowMarkdown", "libraryHideFinishedBooks", "hideFileExtension", "fileBrowserDisplay", "recentBooksView"
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
