#pragma once
#include <HalStorage.h>

#include <string_view>

#include "StageDigest.h"

namespace syncpublish {
enum class CanonicalResult { Clear, MissingParent, NonCanonical, Conflict, Changed, Cancelled, Invalid, Unavailable };
// Confirms each parent by enumerating its actual name and rejects temporary
// siblings in the final directory. It is not a complete publication check.
CanonicalResult inspectCanonicalDestination(HalStorage& storage, std::string_view destination,
                                            syncstage::DigestTick tick, void* context);
}  // namespace syncpublish
