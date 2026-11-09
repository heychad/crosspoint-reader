#pragma once
#include "fontIds.h"
#include "personalization/UiScale.h"

// FreeInkUI font slots. Row heights, header height, and touch sizes are not
// chosen here: FreeInkApp derives its metric tokens from the body font's line
// height (themeTokensForLineHeight).
struct UIScaleSpec {
  int smallFontId;
  int bodyFontId;
  int titleFontId;
};

inline UIScaleSpec uiScaleSpecFor(const uint8_t scale) {
  UIScaleSpec spec{};
  spec.smallFontId = UI_10_FONT_ID;
  spec.bodyFontId = scale == static_cast<uint8_t>(uiscale::Value::Small) ? UI_10_FONT_ID : UI_12_FONT_ID;
  // Titles use the UI font, not a reader font: fui headers draw book and
  // directory titles, and the built-in Ubuntu UI fonts cover Hebrew (plus the
  // size-matched SD CJK fallback) where the NotoSans reader subsets do not.
  // Same font develop's drawHeader used, so script coverage matches develop.
  spec.titleFontId = UI_12_FONT_ID;
  return spec;
}

inline UIScaleSpec uiScaleSpec() { return uiScaleSpecFor(uiscale::current()); }
