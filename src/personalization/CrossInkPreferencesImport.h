#pragma once

#include <ArduinoJson.h>

#include <cstdint>

#include "CrossInkFontName.h"

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
      {"clockFormat", 0, 1},
      {"tapToHideStatusBar", 0, 1},
      {"sleepTimeoutMinutes", 1, 31},
      {"readingIdleTimeThresholdUnits", 3, 60},
      {"paragraphAlignment", 0, 4},
      {"embeddedStyle", 0, 1},
      {"hyphenationEnabled", 0, 1},
      {"extraParagraphSpacing", 0, 1},
      {"textAntiAliasing", 0, 1},
      {"orientation", 0, 3},
      {"sleepScreenCoverMode", 0, 1},
      {"sleepScreenCoverFilter", 0, 2},
      {"quickResumeSleepScreen", 0, 1},
      {"indexingMethod", 0, 1},
      {"fadingFix", 0, 1},
      {"pwrBtnFootnoteBack", 0, 1},
      {"frontlightRestoreOnWake", 0, 1},
      {"frontlightBrightness", 0, 100},
      {"frontlightWarmth", 0, 100},
      {"frontlightOn", 0, 1},
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
  const auto stageMapped = [&](const char* key, uint8_t value) {
    const auto current = proposed[key];
    if ((!replaceExisting && !current.isUnbound()) || (current.is<uint8_t>() && current.as<uint8_t>() == value)) return;
    proposed[key] = value;
    changed = true;
  };
  // A present modern source field owns its value, even when malformed.
  const auto oldDark = legacy["readerDarkMode"];
  if (legacy["screenInverted"].isUnbound() && oldDark.is<uint8_t>() && oldDark.as<uint8_t>() <= 1)
    stageMapped("screenInverted", oldDark.as<uint8_t>());
  const auto oldSleep = legacy["sleepTimeout"];
  if (legacy["sleepTimeoutMinutes"].isUnbound() && oldSleep.is<uint8_t>()) {
    constexpr uint8_t minutes[] = {1, 5, 10, 15, 30};
    const auto value = oldSleep.as<uint8_t>();
    if (value < sizeof(minutes)) stageMapped("sleepTimeoutMinutes", minutes[value]);
  }
  const auto font = legacy["sdFontFamilyName"];
  if (font.is<const char*>()) {
    const auto name = font.as<JsonString>();
    if (crossink::compatibleFontName(name.c_str(), name.size())) {
      const auto currentFont = proposed["sdFontFamilyName"];
      const bool sameFont = currentFont.is<const char*>() && currentFont.as<JsonString>() == name;
      // Keep a different native font and its size together unless replacement was requested.
      if (replaceExisting || (currentFont.isUnbound() && proposed["fontFamily"].isUnbound()) || sameFont) {
        if (!sameFont) {
          proposed["sdFontFamilyName"] = name;
          changed = true;
        }
        const auto size = legacy["fontSize"];
        if (size.is<uint8_t>() && size.as<uint8_t>() >= 8) stageMapped("fontSize", size.as<uint8_t>());
      }
    }
  }
  const auto characterSpacing = legacy["characterSpacing"];
  if (characterSpacing.is<uint8_t>()) {
    const uint8_t raw = characterSpacing.as<uint8_t>();
    // RC uses half-pixel steps around 5; this fork stores whole pixels around 2.
    if (raw >= 1 && raw <= 9 && raw % 2 == 1) stageMapped("characterSpacing", (raw - 1) / 2);
  }
  const auto images = legacy["imageRendering"];
  if (images.is<uint8_t>() && images.as<uint8_t>() <= 2) {
    const uint8_t mode = images.as<uint8_t>();
    const auto grayscale = legacy["imageGrayscale"];
    if (mode != 0) {
      stageMapped("imageRendering", mode);
    } else if (grayscale.isUnbound()) {
      // Older CrossInk settings predate the separate grayscale toggle.
      stageMapped("imageRendering", 0);
    } else if (grayscale.is<uint8_t>() && grayscale.as<uint8_t>() <= 1) {
      stageMapped("imageRendering", grayscale.as<uint8_t>() ? 0 : 3);
    }
  }
  // CrossInk's stored Blank and Cover Custom values differ from this fork.
  // Minimal Stats adds its device overlay only on X3, so both use Minimal on X4 Pro.
  const auto sleep = legacy["sleepScreen"];
  const auto currentSleep = proposed["sleepScreen"];
  if (sleep.is<uint8_t>() && (replaceExisting || currentSleep.isUnbound())) {
    constexpr uint8_t unsupported = 0xff;
    constexpr uint8_t modes[] = {0,  1,  2, 3,  5,  4,
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
                                 14,
#else
                                 unsupported,
#endif
                                 9,  12, 6, 12, 13,
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
                                 15
#else
                                 unsupported
#endif
    };
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
  for (const char* key : {"screenMarginHorizontal", "screenMarginVertical"}) {
    auto value = legacy[key];
    // A present axis owns its source value, including null or malformed values.
    if (value.isUnbound()) value = legacy["screenMargin"];
    const auto current = proposed[key];
    // A native single-margin save already owns both axes.
    if (value.is<uint8_t>() && value.as<uint8_t>() >= 5 && value.as<uint8_t>() <= 150 &&
        (replaceExisting || (current.isUnbound() && proposed["screenMargin"].isUnbound())) &&
        (!current.is<uint8_t>() || current.as<uint8_t>() != value.as<uint8_t>())) {
      proposed[key] = value.as<uint8_t>();
      changed = true;
    }
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
