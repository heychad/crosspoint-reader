#pragma once
#include "StageDigest.h"

namespace syncpublish {
enum class CleanupResult { Cleaned, AlreadyClean, Conflict, Changed, Cancelled, Invalid, Unavailable, Uncertain };
// Prototype only: removes a matching root stage after verifying the archived
// intent and destination. It is not connected to any reader endpoint.
CleanupResult cleanupArchivedStage(HalStorage& storage, const char* stage, syncstage::DigestTick tick, void* context);
}  // namespace syncpublish
