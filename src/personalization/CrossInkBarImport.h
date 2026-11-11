#pragma once

#include <ArduinoJson.h>

#include <array>

#include "ReaderBarsJson.h"

namespace crossinkimport {
enum class BarResult { Imported, Unchanged, Missing, Unsupported, Invalid, Unavailable };

inline BarResult decodeLegacyBar(JsonVariantConst source, ReaderBar& output) {
  const auto slots = source["slots"].as<JsonArrayConst>();
  if (slots.size() != 6 && slots.size() != ReaderBar::SLOT_COUNT) return BarResult::Invalid;
  static constexpr std::array<ReaderBarItem, 11> mapping = {
      ReaderBarItem::Empty,     ReaderBarItem::Clock,        ReaderBarItem::Battery, ReaderBarItem::TimeLeftBook,
      ReaderBarItem::Empty,     ReaderBarItem::ChapterPages, ReaderBarItem::Empty,   ReaderBarItem::BookPercent,
      ReaderBarItem::BookTitle, ReaderBarItem::ChapterTitle, ReaderBarItem::Date};
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
  output = parsed;
  return BarResult::Imported;
}

inline bool equalBars(const ReaderBar& a, const ReaderBar& b) {
  return a.slots == b.slots && a.progress == b.progress && a.thickness == b.thickness &&
         a.percentageDecimals == b.percentageDecimals;
}

// The caller owns the staging document and discards it on Unavailable.
inline BarResult stageBars(JsonVariantConst legacy, JsonDocument& proposed) {
  if (!legacy.is<JsonObjectConst>() || !proposed.is<JsonObject>()) return BarResult::Invalid;
  const auto source = legacy["readerStatusBars"];
  if (source.isUnbound()) return BarResult::Missing;
  if (!source.is<JsonObjectConst>() || !source["version"].is<uint8_t>() || source["version"].as<uint8_t>() != 1)
    return BarResult::Invalid;
  ReaderBar top, bottom;
  const auto topResult = decodeLegacyBar(source["top"], top);
  if (topResult != BarResult::Imported) return topResult;
  const auto bottomResult = decodeLegacyBar(source["bottom"], bottom);
  if (bottomResult != BarResult::Imported) return bottomResult;
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
