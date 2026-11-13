#pragma once

#include <ArduinoJson.h>

#include "ReaderBars.h"

inline void writeReaderBar(JsonObject target, const ReaderBar& bar) {
  auto slots = target["slots"].to<JsonArray>();
  uint8_t percentageMask = 0;
  uint8_t chapterMask = 0;
  for (unsigned i = 0; i < bar.slots.size(); ++i) {
    auto item = bar.slots[i];
    if (item == ReaderBarItem::ChapterPercent) {
      percentageMask |= static_cast<uint8_t>(1U << i);
      item = ReaderBarItem::BookPercent;
    }
#if defined(CROSSPOINT_EXPERIMENTAL_CHAPTER_TIME)
    if (item == ReaderBarItem::TimeLeftChapter) {
      chapterMask |= static_cast<uint8_t>(1U << i);
      item = ReaderBarItem::TimeLeftSection;
    }
#endif
    slots.add(static_cast<uint8_t>(item));
  }
  if (percentageMask)
    target["chapterPercentSlots"] = percentageMask;
  else
    target.remove("chapterPercentSlots");
  if (chapterMask)
    target["chapterTimeSlots"] = chapterMask;
  else
    target.remove("chapterTimeSlots");
  target["progress"] = bar.progress;
  target["thickness"] = bar.thickness;
  target["decimals"] = bar.percentageDecimals;
  target["batteryStyle"] = static_cast<uint8_t>(bar.batteryStyle);
  target["separator"] = static_cast<uint8_t>(bar.separator);
  target["hidden"] = bar.hidden;
  target["centered"] = bar.centered;
}

inline bool readReaderBar(JsonVariantConst source, ReaderBar& bar) {
  const auto slots = source["slots"].as<JsonArrayConst>();
  if (slots.size() != ReaderBar::SLOT_COUNT) return false;
  ReaderBar parsed;
  for (unsigned i = 0; i < ReaderBar::SLOT_COUNT; ++i) {
    // New slot kinds use extension masks, keeping persisted IDs independent of build flags.
    if (!slots[i].is<unsigned>() || slots[i].as<unsigned>() > static_cast<unsigned>(ReaderBarItem::TimeLeftSection))
      return false;
    parsed.slots[i] = static_cast<ReaderBarItem>(slots[i].as<unsigned>());
  }
  const auto percentageExtension = source["chapterPercentSlots"];
  if (percentageExtension.is<unsigned>()) {
    const auto mask = percentageExtension.as<unsigned>();
    bool validMask = mask < (1U << ReaderBar::SLOT_COUNT);
    for (unsigned i = 0; validMask && i < ReaderBar::SLOT_COUNT; ++i)
      if ((mask & (1U << i)) && parsed.slots[i] != ReaderBarItem::BookPercent) validMask = false;
    if (validMask)
      for (unsigned i = 0; i < ReaderBar::SLOT_COUNT; ++i)
        if (mask & (1U << i)) parsed.slots[i] = ReaderBarItem::ChapterPercent;
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
  for (const char* key : {"hidden", "centered"})
    if (!source[key].isUnbound() && !source[key].is<bool>()) return false;
  const auto battery = source["batteryStyle"], separator = source["separator"];
  if (!battery.isUnbound()) {
    if (!battery.is<uint8_t>() || battery.as<uint8_t>() >= static_cast<uint8_t>(ReaderBarBatteryStyle::Count))
      return false;
    parsed.batteryStyle = static_cast<ReaderBarBatteryStyle>(battery.as<uint8_t>());
  }
  if (!separator.isUnbound()) {
    if (!separator.is<uint8_t>() || separator.as<uint8_t>() >= static_cast<uint8_t>(ReaderBarSeparator::Count))
      return false;
    parsed.separator = static_cast<ReaderBarSeparator>(separator.as<uint8_t>());
  }
  parsed.hidden = source["hidden"] | false;
  parsed.centered = source["centered"] | false;
  bar = parsed;
  return true;
}
