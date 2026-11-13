#pragma once

#include "activities/reader/ReadingStatsUtils.h"

struct HomeDashboardDetails {
  uint32_t sessions = 0;
  uint32_t pagesPerMinuteTenths = 0;
  uint32_t averageSessionSeconds = 0;
  uint32_t dailyAverageSeconds = 0;
  uint32_t globalSeconds = 0;
  uint32_t completedBooks = 0;
  ReadingStatsDate startDate;
  ReadingStatsDate finishDate;
  uint16_t daysReading = 0;
  uint16_t currentStreak = 0;
  ReadingTimeBucket readerType = ReadingTimeBucket::Morning;
  bool completed = false;
  bool hasPace = false;
  bool hasClock = false;
  bool hasDays = false;
  bool hasGlobal = false;
  bool hasReaderType = false;
  char chapter[128]{};
};

static_assert(sizeof(HomeDashboardDetails) <= 192, "Review Home dashboard summary size");
