#pragma once

#include <string>

#include "ReadingStatsUtils.h"

// Accept an empty value to clear a date, or a calendar date in YYYY-MM-DD form.
bool parseReadingStatsDateEdit(const std::string& text, ReadingStatsDate& date);
