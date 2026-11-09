#pragma once

#include "SettingsRecoveryPlan.h"

namespace settingsrecovery {
struct Snapshot {
  Candidate primary = Candidate::Unavailable;
  Candidate backup = Candidate::Unavailable;
  Candidate temporary = Candidate::Unavailable;
  Plan decision() const { return plan(primary, backup, temporary); }
  BootPlan bootDecision() const { return bootPlan(primary, backup, temporary); }
};

// Read-only classification of three candidate JSON files under the storage
// lock. No file is selected, renamed, repaired, or deleted by this probe.
Snapshot probe(const char* primary, const char* backup, const char* temporary);
}  // namespace settingsrecovery
