#include "ReaderActivity.h"

#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
#include "CrossPointSettings.h"
#include "ReadingStatsUtils.h"
#include "RecentBooksStore.h"
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
#endif
