#pragma once
enum class StrId {
  STR_STATS_LESS_THAN_MIN,
  STR_STATS_MINUTES_FORMAT,
  STR_STATS_HOURS_MINUTES_FORMAT,
  STR_MONTH_JAN_SHORT,
  STR_MONTH_FEB_SHORT,
  STR_MONTH_MAR_SHORT,
  STR_MONTH_APR_SHORT,
  STR_MONTH_MAY_SHORT,
  STR_MONTH_JUN_SHORT,
  STR_MONTH_JUL_SHORT,
  STR_MONTH_AUG_SHORT,
  STR_MONTH_SEP_SHORT,
  STR_MONTH_OCT_SHORT,
  STR_MONTH_NOV_SHORT,
  STR_MONTH_DEC_SHORT
};
inline const char* testMonthTranslation = nullptr;
inline const char* testTranslate(StrId key) {
  if (key >= StrId::STR_MONTH_JAN_SHORT && key <= StrId::STR_MONTH_DEC_SHORT) {
    constexpr const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return testMonthTranslation ? testMonthTranslation
                                : months[static_cast<int>(key) - static_cast<int>(StrId::STR_MONTH_JAN_SHORT)];
  }
  return key == StrId::STR_STATS_LESS_THAN_MIN    ? "< 1 min"
         : key == StrId::STR_STATS_MINUTES_FORMAT ? "%lu min"
                                                  : "%luh %lu min";
}
#define tr(id) testTranslate(StrId::id)
