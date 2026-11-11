#pragma once
#include "PublishIntentStore.h"
#include "StageDigest.h"

namespace syncpublish {
enum class CopyResult {
  Copied,
  VerifiedExistingCopy,
  ExistingCopy,
  NoIntent,
  Conflict,
  Changed,
  Cancelled,
  ChangedPartial,
  CancelledPartial,
  Invalid,
  Unavailable,
  Uncertain
};
// Copies the verified root stage into one of four private create-only files.
// The source, intent and every partial copy are retained. A matching existing
// copy can be recognized after an interrupted response. This never publishes.
CopyResult copyVerifiedStage(HalStorage& storage, const IntentView& intent, syncstage::DigestTick tick, void* context);
}  // namespace syncpublish
