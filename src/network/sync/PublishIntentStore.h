#pragma once
#include <HalStorage.h>

#include "PublishIntent.h"

namespace syncpublish {
inline constexpr char INTENT_PATH[] = "/.crosspoint/xsync-publish.intent";
enum class PrepareResult { Prepared, Existing, Invalid, Unavailable, Uncertain };
enum class InspectResult { Missing, Valid, Damaged, Unavailable };
// A prepared record is only an intent, not proof of file ownership or a commit.
// The caller must not publish based on this record alone.
PrepareResult prepareIntent(HalStorage& storage, const IntentView& intent);
// On Valid, views borrow output. On other results output and length are untouched.
InspectResult inspectIntent(HalStorage& storage, Record& output, size_t& length);
}  // namespace syncpublish
