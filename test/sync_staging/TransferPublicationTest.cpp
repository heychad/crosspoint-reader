#include <CrossPointState.h>
#include <RecentBooksStore.h>
#include <activities/RenderLock.h>
#include <gtest/gtest.h>

#include "network/sync/TransferPublication.h"

namespace syncpublish {
inline bool called = false;
FinalizeResult finalizeVerifiedCopy(HalStorage&, syncstage::DigestTick tick, void* tickContext, PublicationGate gate,
                                    void* context, PublicationAcquire acquire, PublicationRelease release,
                                    void* lockContext) {
  called = true;
  EXPECT_FALSE(renderHeld);
  EXPECT_TRUE(tick(tickContext));
  if (!acquire(lockContext)) return FinalizeResult::Unavailable;
  EXPECT_TRUE(renderHeld);
  const auto result = gate("/Authors/Book.epub", context) ? FinalizeResult::Published : FinalizeResult::Conflict;
  release(lockContext);
  return result;
}
}  // namespace syncpublish
namespace {
bool tick(void*) { return true; }
TEST(TransferPublicationTest, TakesRenderLockAndChecksLiveBookState) {
  HalStorage storage;
  fakeState.openEpubPath = "/other/Book.epub";
  fakeRecents.books.clear();
  renderBusy = false;
  EXPECT_EQ(syncpublish::finalizeFromTransferActivity(storage, tick, nullptr), syncpublish::FinalizeResult::Conflict);
  EXPECT_FALSE(renderHeld);
  fakeState.openEpubPath.clear();
  fakeRecents.books.push_back({"/else/BOOK.epub"});
  EXPECT_EQ(syncpublish::finalizeFromTransferActivity(storage, tick, nullptr), syncpublish::FinalizeResult::Conflict);
  fakeRecents.books = {{"/else/José.epub"}};
  EXPECT_EQ(syncpublish::finalizeFromTransferActivity(storage, tick, nullptr), syncpublish::FinalizeResult::Published);
  EXPECT_FALSE(renderHeld);
}
TEST(TransferPublicationTest, BusyRendererNeverStartsFinalization) {
  HalStorage storage;
  renderBusy = true;
  syncpublish::called = false;
  EXPECT_EQ(syncpublish::finalizeFromTransferActivity(storage, tick, nullptr),
            syncpublish::FinalizeResult::Unavailable);
  EXPECT_TRUE(syncpublish::called);
  EXPECT_FALSE(renderHeld);
  renderBusy = false;
}
}  // namespace
