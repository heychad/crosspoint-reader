#pragma once
#include <HalStorage.h>

#include <string_view>

#include "StageDigest.h"

namespace syncpublish {
enum class DestinationGuardResult { Clear, Conflict, Invalid, Changed, Cancelled, Unavailable };
// Checks known destination-derived state and sidecar paths under a tracked
// snapshot. Clear does not prove canonical parent names, absence of references,
// unrelated temporary aliases, volume ownership, or safe publication.
DestinationGuardResult inspectDestinationState(HalStorage& storage, std::string_view destination,
                                               syncstage::DigestTick tick, void* context);
}  // namespace syncpublish
