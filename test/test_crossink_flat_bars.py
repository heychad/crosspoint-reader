"""Check legacy bar layout migration in default and chapter-time builds."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PROGRAM = r'''
#include <array>
#include <cassert>
#include <cstdio>
#include <string>
#include "personalization/CrossInkBarImport.h"
using R = crossinkimport::BarResult;
using Item = ReaderBarItem;
std::string json(const JsonDocument& value) {
  std::string output;
  serializeJson(value, output);
  return output;
}
int main() {
  unsigned tested = 0;
  for (int pages : {0, 1}) for (int percent : {0, 1}) for (int battery : {0, 1})
  for (int title : {0, 1, 2}) for (int time : {0, 1, 2}) for (int clock : {0, 1, 2})
  for (int decimals : {0, 1, 2}) for (int progress : {0, 1, 2}) for (int thickness : {0, 1, 2}) {
    JsonDocument source, target;
    source["statusBarChapterPageCount"] = pages;
    source["statusBarBookProgressPercentage"] = percent;
    source["statusBarBattery"] = battery;
    source["statusBarTitle"] = title;
    source["statusBarTimeLeft"] = time;
    source["hideClock"] = clock;
    source["statusBarBookPercentageFormat"] = decimals;
    source["statusBarProgressBar"] = progress;
    source["statusBarProgressBarThickness"] = thickness;
    target["unrelated"] = 42;
    const auto original = json(source);
#if !defined(CROSSPOINT_EXPERIMENTAL_CHAPTER_TIME)
    if (time == 1) {
      const auto before = json(target);
      assert(crossinkimport::stageBars(source.as<JsonVariantConst>(), target) == R::Unsupported);
      assert(json(target) == before && json(source) == original);
      ++tested;
      continue;
    }
#endif
    assert(crossinkimport::stageBars(source.as<JsonVariantConst>(), target) == R::Imported);
    ReaderBar top, bottom;
    assert(readReaderBar(target["readerBarsV1"]["top"], top));
    assert(readReaderBar(target["readerBarsV1"]["bottom"], bottom));
    const Item expectedTime = time == 2 ? Item::TimeLeftBook :
#if defined(CROSSPOINT_EXPERIMENTAL_CHAPTER_TIME)
                              time == 1 ? Item::TimeLeftChapter :
#endif
                              Item::Empty;
    const std::array<Item, 7> expectedBottom = {
      battery ? Item::Battery : expectedTime,
      battery ? expectedTime : Item::Empty,
      Item::Empty,
      title == 0 ? Item::BookTitle : title == 1 ? Item::ChapterTitle : Item::Empty,
      pages ? Item::ChapterPages : percent ? Item::BookPercent : Item::Empty,
      pages && percent ? Item::BookPercent : Item::Empty,
      Item::Empty
    };
    std::array<Item, 7> expectedTop{};
    if (clock == 0) expectedTop[3] = Item::Clock;
    assert(top.slots == expectedTop && bottom.slots == expectedBottom);
    assert(top.progress == 2 && bottom.progress == progress);
    assert(bottom.thickness == thickness && bottom.percentageDecimals == decimals);
    assert(target["readerBarsEnabled"].as<bool>() && target["unrelated"] == 42);
    assert(json(source) == original);
    assert(crossinkimport::stageBars(source.as<JsonVariantConst>(), target) == R::Unchanged);
    ++tested;
  }
  constexpr const char* fields[] = {
    "statusBar", "statusBarChapterPageCount", "stablePageNumbers", "statusBarBookProgressPercentage",
    "statusBarTitle", "statusBarTimeLeft", "statusBarBattery", "statusBarBookPercentageFormat",
    "statusBarProgressBar", "statusBarProgressBarThickness", "hideClock", "statusBarClock"
  };
  for (const char* key : fields) for (const char* value : {"true", "null", "-1", "255", "256", "1.0", "\"1\"", "[]", "{}"}) {
    JsonDocument source, target;
    assert(!deserializeJson(source, std::string("{\"") + key + "\":" + value + "}"));
    target["unrelated"] = 42;
    const auto before = json(target);
    assert(crossinkimport::stageBars(source.as<JsonVariantConst>(), target) == R::Invalid);
    assert(json(target) == before);
  }
  // Coarse legacy presets: chapter pages, percentage, progress, title, battery.
  constexpr int presets[][5] = {{0,0,2,2,0},{0,0,2,1,1},{1,1,2,1,1},
                                {1,0,0,1,1},{1,0,0,2,0},{0,1,1,1,1}};
  for (int preset = 0; preset < 6; ++preset) {
    JsonDocument source, target;
    source["statusBar"] = preset;
    target.to<JsonObject>();
    assert(crossinkimport::stageBars(source.as<JsonVariantConst>(), target) == R::Imported);
    ReaderBar bar;
    assert(readReaderBar(target["readerBarsV1"]["bottom"], bar));
    assert(bar.contains(Item::ChapterPages) == bool(presets[preset][0]));
    assert(bar.contains(Item::BookPercent) == bool(presets[preset][1]));
    assert(bar.progress == presets[preset][2]);
    assert(bar.slots[3] == (presets[preset][3] == 1 ? Item::ChapterTitle : Item::Empty));
    assert(bar.contains(Item::Battery) == bool(presets[preset][4]));
    source["statusBarTitle"] = 0;
    assert(crossinkimport::stageBars(source.as<JsonVariantConst>(), target) == R::Imported);
    assert(readReaderBar(target["readerBarsV1"]["bottom"], bar));
    assert(bar.slots[3] == Item::BookTitle);
  }
  for (int clock : {0,1,2}) {
    JsonDocument source, target;
    source["statusBarClock"] = clock;
    target.to<JsonObject>();
    assert(crossinkimport::stageBars(source.as<JsonVariantConst>(), target) == R::Imported);
    ReaderBar bar;
    assert(readReaderBar(target["readerBarsV1"]["top"], bar));
    assert(bar.contains(Item::Clock) == (clock != 0));
    source["hideClock"] = 1;
    crossinkimport::stageBars(source.as<JsonVariantConst>(), target);
    assert(readReaderBar(target["readerBarsV1"]["top"], bar) && !bar.contains(Item::Clock));
  }
  JsonDocument source, target;
  source["stablePageNumbers"] = 1;
  target["unrelated"] = 42;
  const auto before = json(target);
  assert(crossinkimport::stageBars(source.as<JsonVariantConst>(), target) == R::Unsupported);
  assert(json(target) == before);
  source.clear(); source["pluginsEnabled"] = 1;
  assert(crossinkimport::stageBars(source.as<JsonVariantConst>(), target) == R::Missing);
  assert(json(target) == before);
  // A valid custom layout takes priority; a damaged present layout never falls back.
  assert(!deserializeJson(source, R"({"statusBarTitle":0,"stablePageNumbers":1,"readerStatusBars":{"version":1,
    "top":{"slots":[0,0,0,9,0,0,0],"percentageFormat":0,"progressBar":2,"thickness":1},
    "bottom":{"slots":[0,0,0,0,0,0,0],"percentageFormat":0,"progressBar":2,"thickness":1}}})"));
  assert(crossinkimport::stageBars(source.as<JsonVariantConst>(), target) == R::Imported);
  ReaderBar bar;
  assert(readReaderBar(target["readerBarsV1"]["top"], bar) && bar.slots[3] == Item::ChapterTitle);
  for (const char* invalid : {"null", "false", "{}", "{\"version\":2}"}) {
    source.clear(); source["statusBarTitle"] = 0;
    JsonDocument custom;
    assert(!deserializeJson(custom, invalid));
    source["readerStatusBars"] = custom.as<JsonVariantConst>();
    const auto previous = json(target);
    assert(crossinkimport::stageBars(source.as<JsonVariantConst>(), target) == R::Invalid);
    assert(json(target) == previous);
  }
  std::printf("%u flat-bar combinations and malformed/priority/preset checks passed\n", tested);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    source = directory / 'test.cpp'
    source.write_text(PROGRAM)
    for name, flags in [('default', []), ('chapter', ['-DCROSSPOINT_EXPERIMENTAL_CHAPTER_TIME=1'])]:
        binary = directory / name
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17',
                        *shlex.split(os.environ.get('CXXFLAGS', '')), *flags,
                        '-I', str(ROOT / 'src'), '-I', str(ROOT / '.pio/libdeps/x4pro/ArduinoJson/src'),
                        str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
