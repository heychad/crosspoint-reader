#pragma once
#include <cstdint>

#include "BookReadingStats.h"
#include "GlobalReadingStats.h"

// Adds one finished tracking session to the book and global totals. Sessions of
// at least 60 seconds count as a session; at least 10 seconds add reading time.
// sessionStart may be null when no RTC time was available at session start.
void commitReadingSession(BookReadingStats& book, GlobalReadingStats& global, uint32_t seconds,
                          const ReadingStatsDateTime* sessionStart);
