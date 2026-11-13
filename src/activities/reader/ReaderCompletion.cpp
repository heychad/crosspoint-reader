#include "ReaderActivity.h"

#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
#include <I18n.h>
#include <Memory.h>

#include "CrossPointSettings.h"
#include "ReadingStatsUtils.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "util/ReadFolder.h"
#if defined(CROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION)
#include <FsHelpers.h>
#endif

namespace {
bool removeRecent(const std::string& path, void*) { return RECENT_BOOKS.removeByPathPersisted(path); }
}  // namespace

ReaderStats::CompletionResult ReaderActivity::setBookCompleted(bool completed) {
  RenderLock lock;
  if (!pageRendered.load(std::memory_order_acquire)) return ReaderStats::CompletionResult::Unavailable;
  const bool deferMove = completed && SETTINGS.moveFinishedToReadFolder && !isInReadFolder(bookPath);
#if defined(CROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION)
  if (deferMove && !FsHelpers::hasEpubExtension(bookPath)) return ReaderStats::CompletionResult::Unavailable;
#else
  if (deferMove) return ReaderStats::CompletionResult::Unavailable;
#endif
  ReadingStatsDateTime now{};
  const ReadingStatsDate today = getCurrentLocalReadingStatsDateTime(now) ? now.date : ReadingStatsDate{};
  const bool remove = completed && SETTINGS.removeReadBooksFromRecents;
  return readingStats.editCompletionStats(getBookCachePath(), bookPath, completed, today, remove,
                                          remove ? removeRecent : nullptr, nullptr, deferMove);
}

ReaderStats::CompletionResult ReaderActivity::recoverBookCompletion() {
  RenderLock lock;
  return readingStats.recoverCompletionStats(getBookCachePath(), bookPath, removeRecent);
}
void ReaderActivity::handleCompletionAttempt(bool recovery, bool completed) {
  const auto result = recovery ? recoverBookCompletion() : setBookCompleted(completed);
  if (result == ReaderStats::CompletionResult::Applied || result == ReaderStats::CompletionResult::Unchanged) {
    requestUpdate();
    return;
  }
  if (result == ReaderStats::CompletionResult::UnsupportedMoveSource) {
    auto notice =
        makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_COMPLETION_MOVE_UNSUPPORTED_TITLE),
                                                tr(STR_COMPLETION_MOVE_UNSUPPORTED_BODY), "", tr(STR_OK_BUTTON), true);
    if (!notice) {
      LOG_ERR("READER", "Could not allocate unsupported-move notice");
      requestUpdate();
      return;
    }
    startActivityForResult(std::move(notice), [this](const ActivityResult&) { requestUpdate(); });
    return;
  }
  auto retry = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_COMPLETION_RETRY_TITLE),
                                                       tr(STR_COMPLETION_RETRY_BODY), tr(STR_CANCEL), tr(STR_RETRY));
  if (!retry) {
    LOG_ERR("READER", "Could not allocate completion retry");
    requestUpdate();
    return;
  }
  const bool needsRecovery = recovery || result == ReaderStats::CompletionResult::RecoveryRequired;
  startActivityForResult(std::move(retry), [this, needsRecovery, completed](const ActivityResult& choice) {
    if (choice.isCancelled)
      requestUpdate();
    else
      handleCompletionAttempt(needsRecovery, completed);
  });
}
#endif
