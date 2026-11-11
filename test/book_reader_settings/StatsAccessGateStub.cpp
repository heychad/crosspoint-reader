#include "activities/reader/StatsAccessGate.h"

// This suite isolates reader preferences; completion gating has its own tests.
bool readingstats::normalStatsAllowed() { return true; }
