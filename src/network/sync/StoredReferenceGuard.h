#pragma once
#include <HalStorage.h>

#include <string_view>

#include "StageDigest.h"

namespace syncpublish {
enum class ReferenceGuardResult { Clear, Conflict, Invalid, Changed, Cancelled, Unavailable };
// Conservative filename comparison for both persisted and in-memory paths.
bool referenceMayConflict(std::string_view reference, std::string_view destination);
// Whole-path comparison; ambiguous short names and malformed paths conflict.
bool referenceMayReferTo(std::string_view reference, std::string_view source);
// Checks persisted state/recent files and their backup/temp variants. Matching
// leaf names are conservatively considered conflicts, even across folders.
// It does not inspect unsaved in-memory state or authorize publication.
ReferenceGuardResult inspectStoredReferences(HalStorage& storage, std::string_view destination,
                                             syncstage::DigestTick tick, void* context);
// Checks the same files for references to the original full path after a move.
ReferenceGuardResult inspectStoredSourceReferences(HalStorage& storage, std::string_view source,
                                                   syncstage::DigestTick tick, void* context);
}  // namespace syncpublish
