#pragma once

#include <ArduinoJson.h>

#include <array>

#include "ReaderBarsJson.h"

namespace crossinkimport {
enum class BarResult { Imported, Unchanged, Missing, Unsupported, Invalid, Unavailable };

inline BarResult decodeLegacyBar(JsonVariantConst source, ReaderBar& output) {
  const auto slots = source["slots"].as<JsonArrayConst>();
  if (slots.size() != 6 && slots.size() != ReaderBar::SLOT_COUNT) return BarResult::Invalid;
  static constexpr std::array<ReaderBarItem, 12> mapping = {
      ReaderBarItem::Empty,     ReaderBarItem::Clock,        ReaderBarItem::Battery, ReaderBarItem::TimeLeftBook,
      ReaderBarItem::Empty,     ReaderBarItem::ChapterPages, ReaderBarItem::Empty,   ReaderBarItem::BookPercent,
      ReaderBarItem::BookTitle, ReaderBarItem::ChapterTitle, ReaderBarItem::Date,    ReaderBarItem::ChapterPercent};
  ReaderBar parsed;
  for (size_t i = 0; i < slots.size(); ++i) {
    if (!slots[i].is<uint8_t>() || slots[i].as<uint8_t>() >= mapping.size()) return BarResult::Invalid;
    const uint8_t item = slots[i].as<uint8_t>();
    if (item == 6) return BarResult::Unsupported;
#if defined(CROSSPOINT_EXPERIMENTAL_CHAPTER_TIME)
    if (item == 4) {
      const size_t position = slots.size() == 6 && i >= 2 ? i + 1 : i;
      parsed.slots[position] = ReaderBarItem::TimeLeftChapter;
      continue;
    }
#else
    if (item == 4) return BarResult::Unsupported;
#endif
    const size_t position = slots.size() == 6 && i >= 2 ? i + 1 : i;
    parsed.slots[position] = mapping[item];
  }
  for (const char* key : {"percentageFormat", "progressBar", "thickness"})
    if (!source[key].is<uint8_t>() || source[key].as<uint8_t>() >= 3) return BarResult::Invalid;
  parsed.percentageDecimals = source["percentageFormat"].as<uint8_t>();
  parsed.progress = source["progressBar"].as<uint8_t>();
  parsed.thickness = source["thickness"].as<uint8_t>();
  for (const char* key : {"hidden", "centered"})
    if (!source[key].isUnbound() && !source[key].is<bool>()) return BarResult::Invalid;
  parsed.hidden = source["hidden"] | false;
  parsed.centered = source["centered"] | false;
  const auto battery = source["battery"], separator = source["separator"];
  if (!battery.isUnbound() && (!battery.is<uint8_t>() || battery.as<uint8_t>() > 2)) return BarResult::Invalid;
  if (!separator.isUnbound() && (!separator.is<uint8_t>() || separator.as<uint8_t>() > 3)) return BarResult::Invalid;
  parsed.batteryStyle = static_cast<ReaderBarBatteryStyle>((battery | uint8_t{0}) + 1);
  parsed.separator = static_cast<ReaderBarSeparator>(separator | uint8_t{0});
  output = parsed;
  return BarResult::Imported;
}

inline bool equalBars(const ReaderBar& a, const ReaderBar& b) {
  return a.slots == b.slots && a.progress == b.progress && a.thickness == b.thickness &&
         a.percentageDecimals == b.percentageDecimals && a.batteryStyle == b.batteryStyle &&
         a.separator == b.separator && a.hidden == b.hidden && a.centered == b.centered;
}

inline BarResult decodeFlatLegacyBars(JsonVariantConst source, ReaderBar& top, ReaderBar& bottom) {
  bool present = false;
  for (const char* key :
       {"statusBar", "statusBarChapterPageCount", "stablePageNumbers", "statusBarBookProgressPercentage",
        "statusBarTitle", "statusBarTimeLeft", "statusBarBattery", "statusBarBookPercentageFormat",
        "statusBarProgressBar", "statusBarProgressBarThickness", "hideClock", "statusBarClock"})
    present |= !source[key].isUnbound();
  if (!present) return BarResult::Missing;
  const auto choice = [&](const char* key, uint8_t maximum, uint8_t fallback, uint8_t& output) {
    const auto value = source[key];
    if (value.isUnbound()) {
      output = fallback;
      return true;
    }
    if (!value.is<uint8_t>() || value.as<uint8_t>() > maximum) return false;
    output = value.as<uint8_t>();
    return true;
  };
  struct Preset {
    uint8_t pages, percentage, progress, title, battery;
  };
  // CrossInk's legacy presets; explicit fields override their defaults.
  static constexpr Preset presets[] = {{0, 0, 2, 2, 0}, {0, 0, 2, 1, 1}, {1, 1, 2, 1, 1},
                                       {1, 0, 0, 1, 1}, {1, 0, 0, 2, 0}, {0, 1, 1, 1, 1}};
  uint8_t preset;
  if (!choice("statusBar", 5, 2, preset)) return BarResult::Invalid;
  const auto defaults = presets[preset];
  uint8_t pages, stable, percentage, title, time, battery, decimals, progress, thickness, clock;
  if (!choice("statusBarChapterPageCount", 1, defaults.pages, pages) || !choice("stablePageNumbers", 1, 0, stable) ||
      !choice("statusBarBookProgressPercentage", 1, defaults.percentage, percentage) ||
      !choice("statusBarTitle", 2, defaults.title, title) || !choice("statusBarTimeLeft", 2, 0, time) ||
      !choice("statusBarBattery", 1, defaults.battery, battery) ||
      !choice("statusBarBookPercentageFormat", 2, 0, decimals) ||
      !choice("statusBarProgressBar", 2, defaults.progress, progress) ||
      !choice("statusBarProgressBarThickness", 2, 1, thickness))
    return BarResult::Invalid;
  if (source["hideClock"].isUnbound()) {
    if (!choice("statusBarClock", 2, 0, clock)) return BarResult::Invalid;
    clock = clock == 0 ? 2 : 0;
  } else if (!choice("hideClock", 2, 2, clock)) {
    return BarResult::Invalid;
  }
  if (stable) return BarResult::Unsupported;
  uint8_t hideBattery;
  if (!choice("hideBatteryPercentage", 2, 0, hideBattery)) return BarResult::Invalid;
#if !defined(CROSSPOINT_EXPERIMENTAL_CHAPTER_TIME)
  if (time == 1) return BarResult::Unsupported;
#endif
  ReaderBar parsedTop, parsedBottom;
  parsedTop.batteryStyle = parsedBottom.batteryStyle =
      hideBattery ? ReaderBarBatteryStyle::IconOnly : ReaderBarBatteryStyle::IconAndPercent;
  if (clock == 0) parsedTop.slots[ReaderBar::CENTER] = ReaderBarItem::Clock;
  // Slot placement follows CrossInk's migrateBottomStatusBar (MIT).
  unsigned left = 0, right = ReaderBar::CENTER + 1;
  if (battery) parsedBottom.slots[left++] = ReaderBarItem::Battery;
#if defined(CROSSPOINT_EXPERIMENTAL_CHAPTER_TIME)
  if (time == 1) parsedBottom.slots[left++] = ReaderBarItem::TimeLeftChapter;
#endif
  if (time == 2) parsedBottom.slots[left++] = ReaderBarItem::TimeLeftBook;
  if (title == 0) parsedBottom.slots[ReaderBar::CENTER] = ReaderBarItem::BookTitle;
  if (title == 1) parsedBottom.slots[ReaderBar::CENTER] = ReaderBarItem::ChapterTitle;
  if (pages) parsedBottom.slots[right++] = ReaderBarItem::ChapterPages;
  if (percentage) parsedBottom.slots[right] = ReaderBarItem::BookPercent;
  parsedBottom.percentageDecimals = decimals;
  parsedBottom.progress = progress;
  parsedBottom.thickness = thickness;
  top = parsedTop;
  bottom = parsedBottom;
  return BarResult::Imported;
}

// The caller owns the staging document and discards it on Unavailable.
inline BarResult stageBars(JsonVariantConst legacy, JsonDocument& proposed) {
  if (!legacy.is<JsonObjectConst>() || !proposed.is<JsonObject>()) return BarResult::Invalid;
  const auto source = legacy["readerStatusBars"];
  ReaderBar top, bottom;
  if (source.isUnbound()) {
    const auto result = decodeFlatLegacyBars(legacy, top, bottom);
    if (result != BarResult::Imported) return result;
  } else {
    if (!source.is<JsonObjectConst>() || !source["version"].is<uint8_t>() || source["version"].as<uint8_t>() != 1)
      return BarResult::Invalid;
    const auto topResult = decodeLegacyBar(source["top"], top);
    if (topResult != BarResult::Imported) return topResult;
    const auto bottomResult = decodeLegacyBar(source["bottom"], bottom);
    if (bottomResult != BarResult::Imported) return bottomResult;
    const auto hideBattery = legacy["hideBatteryPercentage"];
    for (const char* key : {"top", "bottom"}) {
      if (!source[key]["battery"].isUnbound() || hideBattery.isUnbound()) continue;
      if (!hideBattery.is<uint8_t>() || hideBattery.as<uint8_t>() > 2) return BarResult::Invalid;
      (key[0] == 't' ? top : bottom).batteryStyle =
          hideBattery.as<uint8_t>() ? ReaderBarBatteryStyle::IconOnly : ReaderBarBatteryStyle::IconAndPercent;
    }
  }
  ReaderBar oldTop, oldBottom;
  if (proposed["readerBarsEnabled"].is<bool>() && proposed["readerBarsEnabled"].as<bool>() &&
      readReaderBar(proposed["readerBarsV1"]["top"], oldTop) &&
      readReaderBar(proposed["readerBarsV1"]["bottom"], oldBottom) && equalBars(top, oldTop) &&
      equalBars(bottom, oldBottom))
    return BarResult::Unchanged;
  auto bars = proposed["readerBarsV1"].to<JsonObject>();
  writeReaderBar(bars["top"].to<JsonObject>(), top);
  writeReaderBar(bars["bottom"].to<JsonObject>(), bottom);
  proposed["readerBarsEnabled"] = true;
  return proposed.overflowed() ? BarResult::Unavailable : BarResult::Imported;
}
}  // namespace crossinkimport
