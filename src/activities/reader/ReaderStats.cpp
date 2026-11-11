#include "ReaderStats.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "BookTracking.h"
#include "CompletionEdit.h"
#include "CompletionTransaction.h"
#include "ReadingStatsStore.h"
#if defined(CROSSPOINT_EXPERIMENTAL_DAILY_STATS)
#include "DailyReadingStatsStore.h"
#endif

void ReaderStats::begin(const std::string& cachePath, const std::string& bookPath, bool trackingEnabled,
                        uint16_t requestedIdleSeconds, bool enableCompletion) {
  startUnavailable = false;
  completionAvailable = false;
  if (!trackingEnabled && !enableCompletion) return;
  if (!lease.acquire()) {
    startUnavailable = trackingEnabled;
    return;
  }
  bool bookEnabled = true;
  bool trackingKnown = false;
  const auto mode =
      trackingEnabled ? BookReadingStats::LoadMode::Repair : BookReadingStats::LoadMode::TransactionalEdit;
  book = readingstats::loadBook(cachePath, bookPath, bookEnabled, nullptr, mode, &trackingKnown);
  if (!trackingKnown) {
    startUnavailable = trackingEnabled;
    lease.release();
    return;
  }
  if ((!bookEnabled || !trackingEnabled) && !enableCompletion) {
    lease.release();
    return;
  }
  const bool track = trackingEnabled && bookEnabled;
  if (track) global = GlobalReadingStats::load();
  completionAvailable = book.writable && (!track || global.writable);
  active = track && completionAvailable;
  startUnavailable = track && !active;
  if (!completionAvailable) lease.release();
  idleSeconds = std::clamp<uint16_t>(requestedIdleSeconds, 30, 600);
  if (completionAvailable) directory = readingstats::bookDirectory(cachePath);
}

void ReaderStats::noteTurn(bool forward, bool succeeded) {
  if (active) session.noteTurn(forward, succeeded, millis());
}

void ReaderStats::onRender(ReadingStatsFrame frame) {
  if (!active) return;
  ReadingStatsDateTime now{};
  const bool hasTime = getCurrentLocalReadingStatsDateTime(now);
  session.onPageRendered(millis(), hasTime ? &now : nullptr, frame, book, global, idleSeconds);
  dirty = dirty || session.pendingSeconds() > 0;
}

#if defined(CROSSPOINT_EXPERIMENTAL_DAILY_STATS)
bool ReaderStats::flushDaily() {
  const auto load = [](void*, uint32_t day, dailyreading::Counter& value) {
    return dailyreading::read(day, value) != dailyreading::ReadResult::Unavailable;
  };
  const auto save = [](void*, uint32_t day, const dailyreading::Counter& value) {
    return dailyreading::write(day, value);
  };
  dailySaveFailed = !session.dailyCounters().flush(nullptr, load, save);
  return !dailySaveFailed;
}
#endif

void ReaderStats::pause() {
  if (active) {
    session.pause(millis(), book, global, idleSeconds);
#if defined(CROSSPOINT_EXPERIMENTAL_DAILY_STATS)
    flushDaily();
#endif
    dirty = dirty || session.pendingSeconds() > 0;
  }
}

bool ReaderStats::snapshot(BookReadingStats& bookOut, GlobalReadingStats& globalOut, bool& saved, bool forceSave) {
  if (!active && !(forceSave && completionAvailable)) return false;
  pause();
  bookOut = book;
  globalOut = global;
  session.preview(bookOut, globalOut);
  saved = (!dirty && !forceSave) || (readingstats::saveBook(directory, bookOut) && globalOut.save());
#if defined(CROSSPOINT_EXPERIMENTAL_DAILY_STATS)
  saved = saved && !dailySaveFailed;
#endif
  return true;
}

ReaderStats::CompletionResult ReaderStats::editCompletionStats(const std::string& cachePath,
                                                               const std::string& bookPath, bool completed,
                                                               const ReadingStatsDate& today, bool removeRecent,
                                                               RemoveRecent remove, void* removeContext,
                                                               bool deferMove) {
  if (!completionAvailable) return CompletionResult::NotTracking;
  if (!readingstats::completionCacheMatchesBook(bookPath, cachePath) ||
      readingstats::bookDirectory(cachePath) != directory ||
      (!today.isValid() && (today.year || today.month || today.day)) || (removeRecent && (!completed || !remove)) ||
      (deferMove && !completed))
    return CompletionResult::Unavailable;
  struct Work {
    ReaderStats* reader;
    const std::string* path;
    bool completed;
    const ReadingStatsDate* today;
    bool removeRecent;
    bool deferMove;
    RemoveRecent remove;
    void* removeContext;
    BookReadingStats snapshotBook;
    GlobalReadingStats snapshotGlobal;
    bool snapshotSaved = false;
    CompletionResult result = CompletionResult::Unavailable;
  };
  auto work =
      makeUniqueNoThrow<Work>(this, &bookPath, completed, &today, removeRecent, deferMove, remove, removeContext);
  if (!work) return CompletionResult::Unavailable;
  const auto edit = [](void* raw) {
    auto& w = *static_cast<Work*>(raw);
    if (!w.reader->active) {
      w.reader->global = GlobalReadingStats::load();
      if (!w.reader->global.writable) return false;
    }
    bool saved = false;
    if (!w.reader->snapshot(w.snapshotBook, w.snapshotGlobal, saved, true) || !saved) {
      w.result = CompletionResult::SaveFailed;
      return false;
    }
    w.snapshotSaved = true;
    const uint8_t policy = (w.removeRecent ? readingstats::completionjournal::REMOVE_RECENTS : 0) |
                           (w.deferMove ? readingstats::completionjournal::DEFER_MOVE_TO_READ : 0);
    const auto result = readingstats::setClosedBookCompletionStats(*w.path, w.completed, *w.today, policy);
    if (result != readingstats::ClosedCompletionResult::Applied &&
        result != readingstats::ClosedCompletionResult::Unchanged) {
      if (result == readingstats::ClosedCompletionResult::Busy)
        w.result = CompletionResult::Busy;
      else if (result == readingstats::ClosedCompletionResult::RecoveryRequired)
        w.result = CompletionResult::RecoveryRequired;
      return false;
    }
    if (w.removeRecent && !w.remove(*w.path, w.removeContext)) {
      w.result = CompletionResult::RecoveryRequired;
      return false;
    }
    const auto finalized = readingstats::finalizeClosedBookCompletion(*w.path, w.completed);
    if (finalized != readingstats::CompletionFinalizeResult::Finalized &&
        finalized != readingstats::CompletionFinalizeResult::NoPending) {
      w.result = CompletionResult::RecoveryRequired;
      return false;
    }
    readingstats::applyCompletion(w.reader->book, w.reader->global, w.completed, *w.today);
    w.result = result == readingstats::ClosedCompletionResult::Applied ? CompletionResult::Applied
                                                                       : CompletionResult::Unchanged;
    return true;
  };
  const auto outcome = lease.withExclusiveSuspension(edit, work.get());
  if (outcome == StatsSessionLease::SuspensionResult::RecoveryRequired) {
    completionRecoveryPending = work->snapshotSaved;
    completionRecoveryTarget = completed;
    completionRecoveryDate = today;
    completionRecoveryWasTracking = active;
    active = false;
    completionAvailable = false;
    startUnavailable = true;
    return CompletionResult::RecoveryRequired;
  }
  if (outcome == StatsSessionLease::SuspensionResult::Busy) return CompletionResult::Busy;
  if (outcome == StatsSessionLease::SuspensionResult::Unavailable ||
      outcome == StatsSessionLease::SuspensionResult::NotHeld)
    return CompletionResult::Unavailable;
  if (work->result == CompletionResult::RecoveryRequired) {
    completionRecoveryPending = work->snapshotSaved;
    completionRecoveryTarget = completed;
    completionRecoveryDate = today;
    completionRecoveryWasTracking = active;
    active = false;
    completionAvailable = false;
    startUnavailable = true;
    lease.release();
  }
  return work->result;
}

ReaderStats::CompletionResult ReaderStats::recoverCompletionStats(const std::string& cachePath,
                                                                  const std::string& bookPath, RemoveRecent remove,
                                                                  void* removeContext) {
  if (active || !completionRecoveryPending || !readingstats::completionCacheMatchesBook(bookPath, cachePath) ||
      readingstats::bookDirectory(cachePath) != directory)
    return CompletionResult::Unavailable;
  struct Recovery {
    ReaderStats* reader;
    const std::string* path;
    RemoveRecent remove;
    void* removeContext;
    CompletionResult result = CompletionResult::RecoveryRequired;
  };
  Recovery work{this, &bookPath, remove, removeContext};
  const auto recover = [](void* raw) {
    auto& w = *static_cast<Recovery*>(raw);
    auto& reader = *w.reader;
    if (!reader.lease.release()) return true;
    bool target = false;
    uint8_t policy = 0;
    if (readingstats::probeClosedBookCompletion(*w.path, target, &policy) !=
            readingstats::CompletionProbeResult::Matching ||
        target != reader.completionRecoveryTarget ||
        (policy &
         ~(readingstats::completionjournal::REMOVE_RECENTS | readingstats::completionjournal::DEFER_MOVE_TO_READ)) ||
        ((policy & readingstats::completionjournal::REMOVE_RECENTS) && !w.remove))
      return true;
    if (readingstats::recoverClosedBookCompletionStats(*w.path, target) !=
        readingstats::CompletionRecoveryResult::Recovered)
      return true;
    if ((policy & readingstats::completionjournal::REMOVE_RECENTS) && !w.remove(*w.path, w.removeContext)) return true;
    if (readingstats::finalizeClosedBookCompletion(*w.path, target) !=
        readingstats::CompletionFinalizeResult::Finalized)
      return true;
    readingstats::applyCompletion(reader.book, reader.global, target, reader.completionRecoveryDate);
    // If reacquisition fails after retirement, leave tracking stopped. A later
    // caller must reload persisted statistics before it can safely resume.
    reader.completionRecoveryPending = false;
    if (!reader.lease.acquire()) return true;
    reader.active = reader.completionRecoveryWasTracking;
    reader.completionAvailable = true;
    reader.startUnavailable = false;
    w.result = CompletionResult::Applied;
    return true;
  };
  return Storage.withStorageLock(recover, &work) ? work.result : CompletionResult::Unavailable;
}

bool ReaderStats::editBookDate(bool started, const ReadingStatsDate& date, BookReadingStats& bookOut,
                               GlobalReadingStats& globalOut, bool& saved) {
  if (!active || (!date.isValid() && (date.year || date.month || date.day))) return false;
  if (started) {
    book.startDate = date;
    book.startDateManual = true;
  } else {
    book.finishedDate = date;
    book.finishedDateManual = true;
  }
  dirty = true;
  return snapshot(bookOut, globalOut, saved);
}

uint32_t ReaderStats::bookTimeLeft(int progressBasisPoints) const {
  if (!active) return 0;
  auto preview = book;
  // The bar is painted before onRender accrues the preceding turn.
  const uint64_t seconds =
      static_cast<uint64_t>(preview.totalReadingSeconds) + session.previewPendingSeconds(idleSeconds);
  preview.totalReadingSeconds = static_cast<uint32_t>(std::min<uint64_t>(seconds, UINT32_MAX));
  return preview.remainingReadingSeconds(progressBasisPoints);
}

uint32_t ReaderStats::sectionTimeLeft(int currentPage, int totalPages) const {
  if (!active || currentPage < 1 || totalPages <= currentPage) return 0;
  auto preview = book;
  session.previewPendingPace(preview, idleSeconds);
  if (preview.paceSampleCount == 0 || preview.avgSecondsPerForwardPage == 0) return 0;
  const uint64_t seconds = static_cast<uint64_t>(totalPages - currentPage) * preview.avgSecondsPerForwardPage;
  return static_cast<uint32_t>(std::min<uint64_t>(seconds, UINT32_MAX));
}

bool ReaderStats::refreshTracking(const std::string& cachePath, const std::string& bookPath, bool globalTracking,
                                  uint16_t requestedIdleSeconds, bool enableCompletion) {
  if (completionRecoveryPending || (!directory.empty() && directory != readingstats::bookDirectory(cachePath)))
    return false;
  const auto state = booktracking::state(cachePath, bookPath);
  if (state == booktracking::State::Unknown) return false;
  const bool requested = globalTracking && state == booktracking::State::Enabled;
  if (requested == active) return true;
  if (!finish()) return false;
  active = false;
  session = {};
  if (!requested) return true;
  begin(cachePath, bookPath, globalTracking, requestedIdleSeconds, enableCompletion);
  return active;
}

statsbackup::BookRestoreResult ReaderStats::resetBookStatistics(const std::string& cachePath,
                                                                const std::string& bookPath, bool globalTracking,
                                                                uint16_t requestedIdleSeconds, bool enableCompletion,
                                                                statsbackup::BookResetScope scope) {
  using Result = statsbackup::BookRestoreResult;
  if (completionRecoveryPending || (!directory.empty() && directory != readingstats::bookDirectory(cachePath)) ||
      !finish())
    return Result::Unavailable;
  struct Request {
    const std::string& cache;
    const std::string& path;
    statsbackup::BookResetScope scope;
    Result result = Result::Unavailable;
  } request{cachePath, bookPath, scope};
  const auto reset = [](void* raw) {
    auto& input = *static_cast<Request*>(raw);
    input.result = statsbackup::resetBookStatistics(input.cache, input.path, nullptr, 0, input.scope);
    return input.result == Result::Restored;
  };
  const auto outcome = lease.withExclusiveSuspension(reset, &request);
  if (outcome == StatsSessionLease::SuspensionResult::NotHeld) reset(&request);
  if (outcome == StatsSessionLease::SuspensionResult::Busy) return Result::Busy;
  if (outcome == StatsSessionLease::SuspensionResult::RecoveryRequired || request.result == Result::WriteUncertain ||
      request.result == Result::RecoveryRequired) {
    active = false;
    completionAvailable = false;
    startUnavailable = true;
    session = {};
    lease.release();
    return Result::RecoveryRequired;
  }
  if (request.result != Result::Restored) return request.result;
  active = false;
  completionAvailable = false;
  session = {};
  if (!lease.release()) {
    startUnavailable = true;
    return Result::Unavailable;
  }
  begin(cachePath, bookPath, globalTracking, requestedIdleSeconds, enableCompletion);
  return Result::Restored;
}

bool ReaderStats::finish() {
  if (!active) return true;
  pause();
  if (!dirty) {
#if defined(CROSSPOINT_EXPERIMENTAL_DAILY_STATS)
    return !dailySaveFailed;
#else
    return true;
#endif
  }
  session.commit(book, global);
  const bool saved = readingstats::saveBook(directory, book) && global.save();
  if (!saved) {
    LOG_ERR("STATS", "Reading statistics save incomplete; previous files retained");
  } else {
    dirty = false;
  }
#if defined(CROSSPOINT_EXPERIMENTAL_DAILY_STATS)
  return saved && !dailySaveFailed;
#else
  return saved;
#endif
}
