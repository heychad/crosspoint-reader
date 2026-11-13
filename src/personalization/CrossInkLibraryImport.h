#pragma once

#include <ArduinoJson.h>

#include <cstdint>

namespace crossinkimport {
inline bool stageLibrary(JsonVariantConst legacy, JsonDocument& proposed, bool replaceExisting) {
  if (!legacy.is<JsonObjectConst>() || !proposed.is<JsonObject>()) return false;
  bool changed = false;
  for (const char* key : {"libraryUseMetadata", "showHiddenFiles", "removeReadBooksFromRecents",
                          "moveFinishedToReadFolder", "libraryShowSeries", "libraryShowGenre", "libraryShowEpub",
                          "libraryShowXtc", "libraryShowTxt", "libraryShowMarkdown", "libraryHideFinishedBooks",
                          "hideFileExtension", "fileBrowserDisplay", "recentBooksView", "libraryListExpanded"}) {
    const auto source = legacy[key];
    const auto current = proposed[key];
    if (!source.is<uint8_t>() || source.as<uint8_t>() > 1 || (!replaceExisting && !current.isUnbound())) continue;
    const uint8_t value = source.as<uint8_t>();
    if (current.is<uint8_t>() && current.as<uint8_t>() == value) continue;
    proposed[key] = value;
    changed = true;
  }
  const auto oldRows = legacy["fileBrowserPreview"];
  const auto currentRows = proposed["fileBrowserDisplay"];
  if (legacy["fileBrowserDisplay"].isUnbound() && oldRows.is<uint8_t>() && oldRows.as<uint8_t>() <= 1 &&
      (replaceExisting || currentRows.isUnbound()) &&
      (!currentRows.is<uint8_t>() || currentRows.as<uint8_t>() != oldRows.as<uint8_t>())) {
    proposed["fileBrowserDisplay"] = oldRows.as<uint8_t>();
    changed = true;
  }
  // CrossInk only applied this setting to its Minimal and Dashboard homes.
  constexpr uint8_t crossInkMinimal = 5;
  constexpr uint8_t crossInkDashboard = 6;
  const auto theme = legacy["uiTheme"];
  const auto currentTheme = proposed["uiTheme"];
  if (theme.is<uint8_t>() && (replaceExisting || currentTheme.isUnbound())) {
    const uint8_t raw = theme.as<uint8_t>();
    // CrossInk 4 is Carousel; the fork uses 4 for Cover Grid.
    if (raw <= 3 || raw == 5 || raw == 6 || raw == 7
#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)
        || raw == 4
#endif
    ) {
      const uint8_t mapped = raw == 7 ? 4 : raw == 4 ? 7 : raw;
      if (!currentTheme.is<uint8_t>() || currentTheme.as<uint8_t>() != mapped) {
        proposed["uiTheme"] = mapped;
        changed = true;
      }
    }
  }
  const auto swap = legacy["swapLibraryFileBrowser"];
  const auto current = proposed["libraryFirstOnHome"];
  if (theme.is<uint8_t>() && (theme.as<uint8_t>() == crossInkMinimal || theme.as<uint8_t>() == crossInkDashboard) &&
      swap.is<uint8_t>() && swap.as<uint8_t>() <= 1 && (replaceExisting || current.isUnbound())) {
    const uint8_t value = swap.as<uint8_t>();
    if (!current.is<uint8_t>() || current.as<uint8_t>() != value) {
      proposed["libraryFirstOnHome"] = value;
      changed = true;
    }
  }
  return changed;
}
}  // namespace crossinkimport
