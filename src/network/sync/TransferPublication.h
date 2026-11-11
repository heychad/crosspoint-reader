#pragma once
#include "FinalizePublication.h"

namespace syncpublish {
// Prototype for the file-transfer activity's main task. Do not call from a
// worker task or a different activity. This function is not a web endpoint.
FinalizeResult finalizeFromTransferActivity(HalStorage& storage, syncstage::DigestTick tick, void* context);
}  // namespace syncpublish
