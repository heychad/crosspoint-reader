#pragma once
#include "StageDigest.h"

namespace syncpublish {
enum class ArchiveResult { Archived, AlreadyArchived, NoIntent, Conflict, Changed, Cancelled, Unavailable, Uncertain };
// Prototype: move a verified intent to a unique archive without deleting the
// upload, destination or partial copies. No production route calls this.
ArchiveResult archiveVerifiedIntent(HalStorage& storage, const char* stage, syncstage::DigestTick tick, void* context);
}  // namespace syncpublish
