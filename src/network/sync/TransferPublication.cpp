#include "TransferPublication.h"

#include <optional>

#include "CrossPointState.h"
#include "MemoryReferenceGuard.h"
#include "RecentBooksStore.h"
#include "activities/RenderLock.h"

namespace syncpublish {
namespace {
bool liveReferencesClear(const char* destination, void*) {
  return memoryReferencesClear(APP_STATE.openEpubPath, RECENT_BOOKS.getBooks(), destination);
}
struct RenderGuard {
  std::optional<RenderLock> lock;
};
bool acquireRender(void* context) {
  auto& guard = *static_cast<RenderGuard*>(context);
  guard.lock.emplace(RenderLock::Mode::Try);
  if (guard.lock->ownsLock()) return true;
  guard.lock.reset();
  return false;
}
void releaseRender(void* context) { static_cast<RenderGuard*>(context)->lock.reset(); }
}  // namespace
FinalizeResult finalizeFromTransferActivity(HalStorage& storage, syncstage::DigestTick tick, void* context) {
  // Main-task activity handling excludes navigation while the render lock
  // excludes the renderer. Other writers still require an ownership audit.
  RenderGuard guard;
  return finalizeVerifiedCopy(storage, tick, context, liveReferencesClear, nullptr, acquireRender, releaseRender,
                              &guard);
}
}  // namespace syncpublish
