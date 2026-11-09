#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "ReadingStatsUtils.h"

namespace readingcharts {
enum class Scope : uint8_t { Book, Device, AllDevices };
enum class Warning : uint8_t { None, Partial, Unsaved };
enum class Day : uint8_t { Unrecorded, Recorded, OutsideHistory };

struct Data {
  std::array<uint32_t, READING_TIME_BUCKET_COUNT> timeOfDay{};
  std::array<uint32_t, READING_DAY_OF_WEEK_COUNT> weekdays{};
  std::array<uint8_t, READING_HISTORY_BYTES> history{};
  uint32_t anchor = 0;
  ReadingStatsDate today{};
  Scope scope = Scope::Book;
  Warning warning = Warning::None;
  bool available = false;
};

inline bool hasHistory(const Data& data) {
  constexpr auto finalMask = (1u << (READING_HISTORY_DAYS % 8)) - 1u;
  return std::any_of(data.history.begin(), data.history.end() - 1, [](uint8_t value) { return value != 0; }) ||
         (data.history.back() & finalMask) != 0;
}

inline int monthIndex(const ReadingStatsDate& date) {
  return date.isValid() ? (date.year - 2000) * 12 + date.month - 1 : -1;
}

inline ReadingStatsDate monthDate(const int index) {
  if (index < 0 || index >= 1200) return {};
  return {static_cast<uint16_t>(2000 + index / 12), static_cast<uint8_t>(index % 12 + 1), 1};
}

struct MonthRange {
  int first = -1;
  int last = -1;
};

inline MonthRange monthRange(const Data& data) {
  ReadingStatsDate anchorDate;
  if (!hasHistory(data) || !readingStatsDateFromDayIndex(data.anchor, anchorDate)) return {};
  ReadingStatsDate oldest;
  const uint32_t firstDay = data.anchor >= READING_HISTORY_DAYS - 1 ? data.anchor - (READING_HISTORY_DAYS - 1) : 0;
  if (!readingStatsDateFromDayIndex(firstDay, oldest)) return {};
  return {monthIndex(oldest), std::max(monthIndex(anchorDate), monthIndex(data.today))};
}

inline Day dayState(const Data& data, const ReadingStatsDate& date) {
  if (!date.isValid() || !hasHistory(data)) return Day::OutsideHistory;
  const uint32_t day = readingStatsDayIndex(date);
  if (day > data.anchor) {
    return data.today.isValid() && day <= readingStatsDayIndex(data.today) ? Day::Unrecorded : Day::OutsideHistory;
  }
  const uint32_t offset = data.anchor - day;
  if (offset >= READING_HISTORY_DAYS) return Day::OutsideHistory;
  return (data.history[offset / 8] & (1u << (offset % 8))) ? Day::Recorded : Day::Unrecorded;
}

inline int barWidth(const uint32_t value, const uint32_t maximum, const int width) {
  if (maximum == 0 || value == 0 || width <= 0) return 0;
  return std::clamp<int>(static_cast<uint64_t>(std::min(value, maximum)) * width / maximum, 1, width);
}
}  // namespace readingcharts
