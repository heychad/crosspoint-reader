#pragma once

#include <cstdint>

namespace settingsrecovery {

// Classification follows a complete, checked read and JSON syntax validation.
// Unavailable means the presence or contents could not be established.
enum class Candidate : uint8_t { Missing, JsonObject, Invalid, Unavailable };
enum class Plan : uint8_t { FirstRun, InspectPrimary, InspectBackup, NeedsReview };

// A read-only boot candidate, not permission to repair or replace files.
// Both selected documents still require settings-specific validation.
enum class BootCandidate : uint8_t { Defaults, Primary, Backup, None };
struct BootPlan {
  BootCandidate candidate;
  bool allowSave;
};

inline BootPlan bootPlan(Candidate primary, Candidate backup, Candidate temporary) {
  if (primary == Candidate::Missing && backup == Candidate::Missing && temporary == Candidate::Missing)
    return {BootCandidate::Defaults, true};
  if (primary == Candidate::JsonObject)
    return {BootCandidate::Primary, backup == Candidate::Missing && temporary == Candidate::Missing};
  // Only a definitely absent primary permits looking at the backup. A staged
  // document is never chosen: it may be an incomplete or uncommitted save.
  if (primary == Candidate::Missing && backup == Candidate::JsonObject)
    return {BootCandidate::Backup, false};
  return {BootCandidate::None, false};
}

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
