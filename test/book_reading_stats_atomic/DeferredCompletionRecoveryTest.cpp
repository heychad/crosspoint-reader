#include <BookReadingStats.h>
#include <CompletionJournal.h>
#include <CompletionJournalIO.h>
#include <CompletionMoveReferences.h>
#include <CompletionTransaction.h>
#include <DeferredCompletionIntentIO.h>
#include <GlobalReadingStats.h>
#include <HalStorage.h>
#include <ReadingStatsStore.h>
#include <RecentBooksStore.h>
#include <StatsSessionLease.h>
#include <activities/ActivityManager.h>
#include <activities/RenderLock.h>
#include <gtest/gtest.h>

#include <functional>
#include <string>

namespace cj = readingstats::completionjournal;
namespace cm = readingstats::completionmove;
namespace dc = readingstats::deferredcompletion;
namespace {
constexpr char BOOK[] = "/Authors/Writer/Recovery.epub";
class DeferredCompletionRecovery : public testing::Test {
 protected:
  void SetUp() override {
    Storage.reset();
    RECENT_BOOKS = {};
    activityManager.main = true;
    testRenderLockAvailable = true;
    HalFile file;
    ASSERT_TRUE(Storage.openFileForWrite("TEST", BOOK, file));
    ASSERT_TRUE(file.close());
    const auto cache = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(BOOK));
    ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(cache), BookReadingStats{}));
    ASSERT_TRUE(GlobalReadingStats{}.save());
  }
};
}  // namespace

TEST_F(DeferredCompletionRecovery, ReplaysAndRetriesRecentsBeforeRetiringJournal) {
  using namespace readingstats;
  ASSERT_EQ(setClosedBookCompletionStats(BOOK, true, {2026, 11, 11}, cj::DEFER_MOVE_TO_READ | cj::REMOVE_RECENTS),
            ClosedCompletionResult::Applied);
  RECENT_BOOKS.success = false;
  EXPECT_EQ(cm::recoverDeferredCompletion(), cm::DeferredRecoveryResult::RecoveryRequired);
  EXPECT_EQ(RECENT_BOOKS.path, BOOK);
  dc::Record record{};
  size_t size = 0;
  unsigned slot = 99;
  EXPECT_EQ(dc::discoverReady(record, size, slot), dc::FindResult::Missing);
  RECENT_BOOKS.success = true;
  EXPECT_EQ(cm::recoverDeferredCompletion(), cm::DeferredRecoveryResult::Recovered);
  EXPECT_EQ(RECENT_BOOKS.calls, 2);
  EXPECT_EQ(dc::discoverReady(record, size, slot), dc::FindResult::Found);
  EXPECT_TRUE(Storage.exists(BOOK));
}

TEST_F(DeferredCompletionRecovery, RespectsThreadLockAndOtherJournal) {
  using namespace readingstats;
  EXPECT_EQ(cm::recoverDeferredCompletion(), cm::DeferredRecoveryResult::NoPending);
  ASSERT_EQ(setClosedBookCompletionStats(BOOK, true, {2026, 11, 11}, cj::DEFER_MOVE_TO_READ),
            ClosedCompletionResult::Applied);
  activityManager.main = false;
  EXPECT_EQ(cm::recoverDeferredCompletion(), cm::DeferredRecoveryResult::Unavailable);
  activityManager.main = true;
  testRenderLockAvailable = false;
  EXPECT_EQ(cm::recoverDeferredCompletion(), cm::DeferredRecoveryResult::Unavailable);
  testRenderLockAvailable = true;
  EXPECT_EQ(cm::recoverDeferredCompletion(), cm::DeferredRecoveryResult::Recovered);
  ASSERT_EQ(setClosedBookCompletionStats(BOOK, false, {2026, 11, 11}), ClosedCompletionResult::Applied);
  EXPECT_EQ(cm::recoverDeferredCompletion(), cm::DeferredRecoveryResult::Busy);
  EXPECT_EQ(RECENT_BOOKS.calls, 0);
}
