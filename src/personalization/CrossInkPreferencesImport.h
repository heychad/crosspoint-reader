#pragma once

#include <ArduinoJson.h>

#include <cstdint>

namespace crossinkimport {
inline bool stagePreferences(JsonVariantConst legacy, JsonDocument& proposed, bool replaceExisting) {
  if (!legacy.is<JsonObjectConst>() || !proposed.is<JsonObject>()) return false;
  struct Field {
    const char* key;
    uint8_t min;
    uint8_t max;
  };
  static constexpr Field fields[] = {
      {"uiScale", 0, 1},
      {"trackReadingStats", 0, 1},
      {"refreshFrequency", 0, 5},
      {"screenInverted", 0, 1},
      {"hideBatteryPercentage", 0, 2},
      {"sleepTimeoutMinutes", 1, 31},
      {"readingIdleTimeThresholdUnits", 3, 60},
      {"paragraphAlignment", 0, 4},
      {"embeddedStyle", 0, 1},
      {"hyphenationEnabled", 0, 1},
      {"extraParagraphSpacing", 0, 1},
      {"textAntiAliasing", 0, 1},
      {"imageRendering", 0, 2},
      {"orientation", 0, 3},
      {"sleepScreenCoverMode", 0, 1},
      {"sleepScreenCoverFilter", 0, 2},
      {"quickResumeSleepScreen", 0, 1},
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_BACKUP)
      {"autoBackupStats", 0, 1},
#endif
  };
  bool changed = false;
  for (const auto& field : fields) {
    const auto value = legacy[field.key];
    const auto current = proposed[field.key];
    if (!value.is<uint8_t>() || value.as<uint8_t>() < field.min || value.as<uint8_t>() > field.max ||
        (!replaceExisting && !current.isUnbound()))
      continue;
    if (current.is<uint8_t>() && current.as<uint8_t>() == value.as<uint8_t>()) continue;
    proposed[field.key] = value.as<uint8_t>();
    changed = true;
  }
  // CrossInk's stored Blank and Cover Custom values differ from this fork.
  // Minimal Stats shares Minimal on X4 Pro.
  const auto sleep = legacy["sleepScreen"];
  const auto currentSleep = proposed["sleepScreen"];
  if (sleep.is<uint8_t>() && (replaceExisting || currentSleep.isUnbound())) {
    constexpr uint8_t unsupported = 0xff;
    constexpr uint8_t modes[] = {0,  1,  2, 3,  5, 4,
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
                                 14,
#else
                                 unsupported,
#endif
                                 9,  12, 6, 12, 13};
    const uint8_t raw = sleep.as<uint8_t>();
    if (raw < sizeof(modes) && modes[raw] != unsupported) {
      const uint8_t mapped = modes[raw];
      if (!currentSleep.is<uint8_t>() || currentSleep.as<uint8_t>() != mapped) {
        proposed["sleepScreen"] = mapped;
        changed = true;
      }
    }
  }
  const auto legacyHeight = legacy["lineHeightPercent"];
  const auto currentHeight = proposed["lineHeight"];
  const auto oldHeight = proposed["lineSpacing"];
  if (legacyHeight.is<uint8_t>() && (replaceExisting || (currentHeight.isUnbound() && oldHeight.isUnbound()))) {
    static constexpr uint8_t heights[] = {100, 110, 120, 130, 145, 160, 175};
    for (uint8_t i = 0; i < sizeof(heights); ++i) {
      if (legacyHeight.as<uint8_t>() != heights[i]) continue;
      if (!currentHeight.is<uint8_t>() || currentHeight.as<uint8_t>() != i) {
        proposed["lineHeight"] = i;
        changed = true;
      }
      break;
    }
  }
  const auto verticalMargin = legacy["screenMarginVertical"];
  const auto horizontalMargin = legacy["screenMarginHorizontal"];
  const auto currentMargin = proposed["screenMargin"];
  if (verticalMargin.is<uint8_t>() && horizontalMargin.is<uint8_t>() && verticalMargin.as<uint8_t>() >= 5 &&
      verticalMargin.as<uint8_t>() <= 40 && verticalMargin.as<uint8_t>() == horizontalMargin.as<uint8_t>() &&
      (replaceExisting || currentMargin.isUnbound()) &&
      (!currentMargin.is<uint8_t>() || currentMargin.as<uint8_t>() != verticalMargin.as<uint8_t>())) {
    proposed["screenMargin"] = verticalMargin.as<uint8_t>();
    changed = true;
  }
  const auto legacyWordSpacing = legacy["wordSpacing"];
  const auto currentWordSpacing = proposed["wordSpacing"];
  if (legacyWordSpacing.is<uint8_t>() && legacyWordSpacing.as<uint8_t>() == 0 &&
      (replaceExisting || currentWordSpacing.isUnbound()) &&
      (!currentWordSpacing.is<uint8_t>() || currentWordSpacing.as<uint8_t>() != 100)) {
    proposed["wordSpacing"] = 100;
    changed = true;
  }
  return changed;
}
}  // namespace crossinkimport
