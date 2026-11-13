#pragma once

#include <ArduinoJson.h>

#include <cstdint>

namespace libraryview {
enum class Tab : uint8_t { Recent, Title, Author, Series, Genre, Count };
inline constexpr uint8_t TAB_COUNT = static_cast<uint8_t>(Tab::Count);
inline constexpr uint8_t DIRECTION_MASK = (1u << TAB_COUNT) - 1;

struct Preferences {
  uint8_t tab = 0;
  uint8_t descendingTabs = 1;  // Recent starts newest first.

  void read(JsonVariantConst doc) {
    const auto savedTab = doc["libraryViewTab"];
    const auto directions = doc["libraryViewDescendingTabs"];
    tab = savedTab.is<uint8_t>() && savedTab.as<uint8_t>() < TAB_COUNT ? savedTab.as<uint8_t>() : 0;
    descendingTabs =
        directions.is<uint8_t>() && directions.as<uint8_t>() <= DIRECTION_MASK ? directions.as<uint8_t>() : 1;
  }

  void write(JsonDocument& doc) const {
    doc["libraryViewTab"] = tab;
    doc["libraryViewDescendingTabs"] = descendingTabs;
  }
};
static_assert(sizeof(Preferences) == 2);
}  // namespace libraryview
