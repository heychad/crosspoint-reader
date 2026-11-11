#pragma once
#include <HalStorage.h>

#include <string_view>

#include "StageDigest.h"

namespace syncpublish {
enum class ParentState { Ready, Conflict, Changed, Cancelled, Invalid, Unavailable, Uncertain };
struct ParentResult {
  ParentState state;
  unsigned created;
};
// Creates only missing parent directories for a validated destination. Any
// created directories are retained on failure; this does not reserve a path.
ParentResult ensureParents(HalStorage& storage, std::string_view destination, syncstage::DigestTick tick,
                           void* context);
}  // namespace syncpublish
