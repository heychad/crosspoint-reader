#pragma once

#include <ArduinoJson.h>

#include "ReaderBars.h"

inline void writeReaderBar(JsonObject target, const ReaderBar& bar) {
  auto slots = target["slots"].to<JsonArray>();
  for (auto item : bar.slots) slots.add(static_cast<uint8_t>(item));
  target["progress"] = bar.progress;
  target["thickness"] = bar.thickness;
  target["decimals"] = bar.percentageDecimals;
}

inline bool readReaderBar(JsonVariantConst source, ReaderBar& bar) {
  const auto slots = source["slots"].as<JsonArrayConst>();
  if (slots.size() != ReaderBar::SLOT_COUNT) return false;
  ReaderBar parsed;
  for (unsigned i = 0; i < ReaderBar::SLOT_COUNT; ++i) {
    if (!slots[i].is<unsigned>() || slots[i].as<unsigned>() >= static_cast<unsigned>(ReaderBarItem::Count))
      return false;
    parsed.slots[i] = static_cast<ReaderBarItem>(slots[i].as<unsigned>());
  }
  for (auto key : {"progress", "thickness", "decimals"}) {
    if (!source[key].is<unsigned>() || source[key].as<unsigned>() >= 3) return false;
  }
  parsed.progress = source["progress"].as<uint8_t>();
  parsed.thickness = source["thickness"].as<uint8_t>();
  parsed.percentageDecimals = source["decimals"].as<uint8_t>();
  bar = parsed;
  return true;
}
