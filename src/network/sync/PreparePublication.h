#pragma once
#include "PublishIntentStore.h"
#include "StageDigest.h"

namespace syncpublish {
enum class PreflightResult {
  Prepared,
  ExistingIntent,
  StageMissing,
  HashMismatch,
  ParentMissing,
  NonCanonical,
  DestinationPresent,
  ReservedState,
  Changed,
  Cancelled,
  Invalid,
  Unavailable,
  Uncertain
};
// Creates an intent only after a checked hash and a repeated target check.
// It does not establish filesystem ownership or authorize publication.
PreflightResult prepareVerifiedIntent(HalStorage& storage, const IntentView& intent, syncstage::DigestTick tick,
                                      void* context);
}  // namespace syncpublish
