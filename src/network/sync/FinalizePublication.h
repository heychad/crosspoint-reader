#pragma once
#include "StageDigest.h"

namespace syncpublish {
enum class FinalizeResult {
  Published,
  MatchingDestination,
  NoIntent,
  Conflict,
  Changed,
  Cancelled,
  Unavailable,
  Uncertain
};
// Prototype only. The caller must provide exclusive ownership of all in-memory
// book references from lock acquisition through release, as well as the
// intended mounted SD card. No production caller is authorized until those
// guarantees and filesystem ownership are reviewed.
using PublicationGate = bool (*)(const char* destination, void*);
using PublicationAcquire = bool (*)(void*);
using PublicationRelease = void (*)(void*);
FinalizeResult finalizeVerifiedCopy(HalStorage& storage, syncstage::DigestTick tick, void* tickContext,
                                    PublicationGate gate, void* gateContext, PublicationAcquire acquire,
                                    PublicationRelease release, void* lockContext);
}  // namespace syncpublish
