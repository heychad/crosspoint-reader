#pragma once

#include "DailyReadingStatsCodec.h"

namespace dailyreading {
constexpr const char* DIRECTORY = "/.crosspoint/daily_reading";
enum class ReadResult : uint8_t { Available, Missing, Unavailable };
// Available describes stored data, not proof that every zero day was tracked.
ReadResult read(uint32_t day, Counter& result);
// Absolute counters make retry idempotent. Totals and acknowledgments cannot decrease.
bool write(uint32_t day, const Counter& counter);
}  // namespace dailyreading
