#pragma once

#include <ArduinoJson.h>

#include "ReaderBars.h"

inline void writeReaderBar(JsonObject target, const ReaderBar& bar) {
  auto slots = target["slots"].to<JsonArray>();
#if defined(CROSSPOINT_EXPERIMENTAL_CHAPTER_TIME)
  uint8_t chapterMask = 0;
  for (unsigned i = 0; i < bar.slots.size(); ++i) {
    auto item = bar.slots[i];
    if (item == ReaderBarItem::TimeLeftChapter) {
      chapterMask |= static_cast<uint8_t>(1U << i);
      item = ReaderBarItem::TimeLeftSection;
    }
    slots.add(static_cast<uint8_t>(item));
  }
  if (chapterMask)
    target["chapterTimeSlots"] = chapterMask;
  else
    target.remove("chapterTimeSlots");
#else
  for (auto item : bar.slots) slots.add(static_cast<uint8_t>(item));
#endif
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
#if defined(CROSSPOINT_EXPERIMENTAL_CHAPTER_TIME)
  const auto extension = source["chapterTimeSlots"];
  if (extension.is<unsigned>()) {
    const auto mask = extension.as<unsigned>();
    bool validMask = mask < (1U << ReaderBar::SLOT_COUNT);
    for (unsigned i = 0; validMask && i < ReaderBar::SLOT_COUNT; ++i)
      if ((mask & (1U << i)) && parsed.slots[i] != ReaderBarItem::TimeLeftSection) validMask = false;
    if (validMask)
      for (unsigned i = 0; i < ReaderBar::SLOT_COUNT; ++i)
        if (mask & (1U << i)) parsed.slots[i] = ReaderBarItem::TimeLeftChapter;
  }
#endif
  for (auto key : {"progress", "thickness", "decimals"}) {
    if (!source[key].is<unsigned>() || source[key].as<unsigned>() >= 3) return false;
  }
  parsed.progress = source["progress"].as<uint8_t>();
  parsed.thickness = source["thickness"].as<uint8_t>();
  parsed.percentageDecimals = source["decimals"].as<uint8_t>();
  bar = parsed;
  return true;
}
