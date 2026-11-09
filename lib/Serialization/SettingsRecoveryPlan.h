#pragma once

#include <cstdint>

namespace settingsrecovery {

// Classification follows a complete, checked read and JSON syntax validation.
// Unavailable means the presence or contents could not be established.
enum class Candidate : uint8_t { Missing, JsonObject, Invalid, Unavailable };
enum class Plan : uint8_t { FirstRun, InspectPrimary, InspectBackup, NeedsReview };

// This function makes no filesystem changes. A backup is read-only until a
// separate, checked recovery transaction is implemented. An object is only
// syntactically valid; provenance and settings compatibility remain unknown.
inline Plan plan(Candidate primary, Candidate backup, Candidate temporary) {
  if (primary == Candidate::Missing && backup == Candidate::Missing && temporary == Candidate::Missing)
    return Plan::FirstRun;
  if (primary == Candidate::JsonObject && backup == Candidate::Missing && temporary == Candidate::Missing)
    return Plan::InspectPrimary;
  if (primary == Candidate::Missing && backup == Candidate::JsonObject && temporary == Candidate::Missing)
    return Plan::InspectBackup;
  return Plan::NeedsReview;
}

}  // namespace settingsrecovery
