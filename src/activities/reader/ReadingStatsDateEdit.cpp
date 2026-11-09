#include "ReadingStatsDateEdit.h"

bool parseReadingStatsDateEdit(const std::string& text, ReadingStatsDate& date) {
  if (text.empty()) {
    date = {};
    return true;
  }
  if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
  for (size_t i = 0; i < text.size(); ++i) {
    if (i == 4 || i == 7) continue;
    if (text[i] < '0' || text[i] > '9') return false;
  }
  ReadingStatsDate candidate;
  candidate.year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 + (text[2] - '0') * 10 + text[3] - '0';
  candidate.month = (text[5] - '0') * 10 + text[6] - '0';
  candidate.day = (text[8] - '0') * 10 + text[9] - '0';
  if (!candidate.isValid()) return false;
  date = candidate;
  return true;
}
