#pragma once

#include <ArduinoJson.h>

#include <cstdint>

namespace libraryview {
enum class Tab : uint8_t { Recent, Title, Author, Series, Genre, AuthorFirst, RecentlyOpened, Count };
inline constexpr uint8_t TAB_COUNT = static_cast<uint8_t>(Tab::Count);
inline constexpr uint8_t MAIN_TAB_COUNT = static_cast<uint8_t>(Tab::Genre) + 1;
inline constexpr uint8_t DIRECTION_MASK = (1u << TAB_COUNT) - 1;
inline constexpr uint8_t DEFAULT_DIRECTIONS = 1u | (1u << static_cast<uint8_t>(Tab::RecentlyOpened));

inline constexpr uint8_t mainTab(const uint8_t view) {
  if (view == static_cast<uint8_t>(Tab::RecentlyOpened)) return static_cast<uint8_t>(Tab::Recent);
  return view == static_cast<uint8_t>(Tab::AuthorFirst) ? static_cast<uint8_t>(Tab::Author) : view;
}

struct Preferences {
  uint8_t tab = 0;
  uint8_t descendingTabs = DEFAULT_DIRECTIONS;

  void read(JsonVariantConst doc) {
    const auto savedTab = doc["libraryViewTab"];
    const auto directions = doc["libraryViewDescendingTabs"];
    tab = savedTab.is<uint8_t>() && savedTab.as<uint8_t>() < TAB_COUNT ? savedTab.as<uint8_t>() : 0;
    descendingTabs = directions.is<uint8_t>() && directions.as<uint8_t>() <= DIRECTION_MASK ? directions.as<uint8_t>()
                                                                                            : DEFAULT_DIRECTIONS;
  }

  void write(JsonDocument& doc) const {
    doc["libraryViewTab"] = tab;
    doc["libraryViewDescendingTabs"] = descendingTabs;
  }
};
static_assert(sizeof(Preferences) == 2);
}  // namespace libraryview
