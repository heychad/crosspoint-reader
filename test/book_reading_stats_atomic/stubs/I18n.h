#pragma once
inline constexpr int STR_STATS_LESS_THAN_MIN = 0;
inline constexpr int STR_STATS_MINUTES_FORMAT = 1;
inline constexpr int STR_STATS_HOURS_MINUTES_FORMAT = 2;
inline const char* tr(int key) {
  return key == STR_STATS_LESS_THAN_MIN ? "< 1 min" : key == STR_STATS_MINUTES_FORMAT ? "%lu min" : "%luh %lu min";
}
