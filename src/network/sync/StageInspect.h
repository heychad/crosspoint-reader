#pragma once
#include "StageDigest.h"

namespace syncstage {
enum class StageInspectResult { Complete, Missing, WrongSize, Changed, Cancelled, Invalid, Unavailable };
// Read-only snapshot. Output changes only when Complete; no file is reserved.
StageInspectResult inspectStage(HalStorage& storage, const char* name, uint64_t size, uint8_t (&digest)[32],
                                DigestTick tick, void* context);
}  // namespace syncstage
