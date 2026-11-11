#include "ReadingStatsActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "activities/reader/BookStatsBackup.h"
#include "activities/reader/BookTracking.h"
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
#include <FsHelpers.h>

#include "RecentBooksStore.h"
#include "activities/reader/CompletionJournal.h"
#include "activities/reader/CompletionTransaction.h"
#include "util/ReadFolder.h"
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION_MOVE)
#include <Epub.h>
#include <HalStorage.h>

#include "activities/reader/CompletionMovePlanIO.h"
#include "activities/reader/CompletionMoveReferences.h"
#endif
#endif
#include "activities/reader/DeviceStatsBackup.h"
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
#include "activities/home/FileBrowserActivity.h"
#include "activities/reader/CompletionTransaction.h"
#include "activities/util/ConfirmationActivity.h"
#endif
#include "activities/reader/ReaderStats.h"
#include "activities/reader/ReadingStatsDateEdit.h"
#include "activities/reader/ReadingStatsStore.h"
#include "activities/reader/StatsSaveNotice.h"
#include "activities/settings/ReadingChartsActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
namespace {
readingstats::ClosedCompletionResult attemptCompletion(const std::string& path, bool desired,
                                                       const ReadingStatsDate& today, bool retry,
                                                       std::string* movedPath = nullptr, bool allowMove = true) {
  namespace cj = readingstats::completionjournal;
  uint8_t policy = 0;
  if (desired && SETTINGS.removeReadBooksFromRecents) policy |= cj::REMOVE_RECENTS;
  if (desired && SETTINGS.moveFinishedToReadFolder && FsHelpers::hasEpubExtension(path) && !isInReadFolder(path))
    policy |= cj::MOVE_TO_READ;
  if ((policy & cj::MOVE_TO_READ) && !allowMove) return readingstats::ClosedCompletionResult::Unavailable;
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION_MOVE)
  std::string destination;
  if (policy & cj::MOVE_TO_READ) {
    if (!movedPath) return readingstats::ClosedCompletionResult::Unavailable;
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos || slash + 1 == path.size()) return readingstats::ClosedCompletionResult::Invalid;
    destination = "/read/" + path.substr(slash + 1);
    if (!retry && Storage.lookupPath(destination.c_str()) != HalStorage::PathStatus::Missing)
      return readingstats::ClosedCompletionResult::Unavailable;
  }
#else
  (void)movedPath;
  (void)allowMove;
#endif
  bool recovered = false;
  if (retry) {
    bool target = false;
    uint8_t recordedPolicy = 0;
    const auto probe = readingstats::probeClosedBookCompletion(path, target, &recordedPolicy);
    if (probe == readingstats::CompletionProbeResult::Matching) {
      if (target != desired) return readingstats::ClosedCompletionResult::RecoveryRequired;
      policy = recordedPolicy;
      if ((policy & cj::MOVE_TO_READ) && !allowMove) return readingstats::ClosedCompletionResult::Unavailable;
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION_MOVE)
      if ((policy & cj::MOVE_TO_READ) && destination.empty()) {
        if (!allowMove || !movedPath) return readingstats::ClosedCompletionResult::Unavailable;
        const auto slash = path.find_last_of('/');
        if (slash == std::string::npos || slash + 1 == path.size())
          return readingstats::ClosedCompletionResult::Invalid;
        destination = "/read/" + path.substr(slash + 1);
      }
#endif
    } else if (probe != readingstats::CompletionProbeResult::NoPending) {
      return readingstats::ClosedCompletionResult::RecoveryRequired;
    }
    switch (readingstats::recoverClosedBookCompletionStats(path, desired)) {
      case readingstats::CompletionRecoveryResult::Recovered:
        recovered = true;
        break;
      case readingstats::CompletionRecoveryResult::NoPending:
        break;
      case readingstats::CompletionRecoveryResult::Busy:
        return readingstats::ClosedCompletionResult::Busy;
      case readingstats::CompletionRecoveryResult::DifferentTransaction:
      case readingstats::CompletionRecoveryResult::ManualReview:
        return readingstats::ClosedCompletionResult::RecoveryRequired;
      case readingstats::CompletionRecoveryResult::Unavailable:
        return readingstats::ClosedCompletionResult::Unavailable;
    }
  }
  const auto result = recovered ? readingstats::ClosedCompletionResult::Unchanged
                                : readingstats::setClosedBookCompletionStats(path, desired, today, policy);
  if (result != readingstats::ClosedCompletionResult::Applied &&
      result != readingstats::ClosedCompletionResult::Unchanged)
    return result;
  if ((policy & cj::REMOVE_RECENTS) && !RECENT_BOOKS.removeByPathPersisted(path))
    return readingstats::ClosedCompletionResult::Unavailable;
  if (policy & cj::MOVE_TO_READ) {
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION_MOVE)
    namespace cm = readingstats::completionmove;
    if (destination.empty() || cm::preparePlan(path, destination) != cm::PlanResult::Ready ||
        cm::updateMoveReferences() != cm::ReferenceResult::Updated)
      return readingstats::ClosedCompletionResult::RecoveryRequired;
    *movedPath = std::move(destination);
    return result;
#else
    return readingstats::ClosedCompletionResult::RecoveryRequired;
#endif
  }
  const auto finalized = readingstats::finalizeClosedBookCompletion(path, desired);
  if (finalized != readingstats::CompletionFinalizeResult::Finalized &&
      finalized != readingstats::CompletionFinalizeResult::NoPending)
    return readingstats::ClosedCompletionResult::RecoveryRequired;
  return result;
}
}  // namespace
#endif

ReadingStatsActivity::ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiTabListActivity("ReadingStats", renderer, mappedInput) {}

ReadingStatsActivity::ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const std::string& title, const BookReadingStats& book,
                                           const GlobalReadingStats& global, int progress, bool tracking, bool saved,
                                           const std::string& cachePath, const std::string& bookPath,
                                           ReaderStats* liveStats, bool closedBookDateEdit)
    : UiTabListActivity("ReadingStats", renderer, mappedInput),
      book(book),
      local(global),
      progress(std::clamp(progress, -1, 10000)),
      tab(Tab::Book),
      hasBook(true),
      tracking(tracking),
      saved(saved),
      liveStats(liveStats),
      closedBookDateEdit(closedBookDateEdit),
      trackingCachePath(cachePath),
      trackingBookPath(bookPath) {
  size_t length = std::min(title.size(), sizeof(bookTitle) - 1);
  // Keep the bounded caption on a UTF-8 codepoint boundary.
  while (length && length < title.size() && (static_cast<unsigned char>(title[length]) & 0xc0) == 0x80) --length;
  memcpy(bookTitle, title.data(), length);
  bookTitle[length] = '\0';
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
  completionRecoveryChecking = closedBookDateEdit && !liveStats && (!book.writable || !global.writable);
#endif
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
  if (closedBookDateEdit && !liveStats && (!book.writable || !global.writable))
    bookRestoreRecovery = BookRestoreRecovery::Checking;
#endif
}

void ReadingStatsActivity::onEnter() {
  if (!hasBook) local = GlobalReadingStats::load();
  aggregate = GlobalReadingStats::loadAggregated(local);
  getCurrentLocalReadingStatsDateTime(now);
  UiTabListActivity::onEnter();
}

void ReadingStatsActivity::savePendingDateBeforeExit() {
  if (!closedDateSavePending) return;
  BookReadingStats updated;
  if (!readingstats::editClosedBookDate(trackingCachePath, trackingBookPath, dateResultStarted, pendingClosedDate,
                                        updated))
    statsSaveNotice().recordFailure();
  closedDateSavePending = false;
}

void ReadingStatsActivity::onExit() {
  savePendingDateBeforeExit();
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
  savePendingCompletionBeforeExit();
#endif
  UiTabListActivity::onExit();
}

void ReadingStatsActivity::prepareForSleep() {
  savePendingDateBeforeExit();
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
  savePendingCompletionBeforeExit();
#endif
}

const char* ReadingStatsActivity::headerTitle() const { return tr(STR_READING_STATISTICS); }

const char* ReadingStatsActivity::tabLabel(int index) const {
  static constexpr StrId labels[] = {StrId::STR_STATS_BOOK, StrId::STR_STATS_DEVICE, StrId::STR_STATS_ALL_DEVICES};
  return I18N.get(labels[index + (hasBook ? 0 : 1)]);
}

int ReadingStatsActivity::listCount() const {
  constexpr int extraCount =
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
      6;
#else
      4;
#endif
  return (bookTab() ? std::size(BOOK_ROWS) : std::size(GLOBAL_ROWS)) +
         (tab == Tab::Device       ? extraCount
          : tab == Tab::AllDevices ? 3
                                   : 0) +
         static_cast<int>(Row::Sunday) - static_cast<int>(Row::Morning) + 1;
}

ReadingStatsActivity::Row ReadingStatsActivity::rowAt(int index) const {
  const int basic = bookTab() ? std::size(BOOK_ROWS) : std::size(GLOBAL_ROWS);
  if (index < basic) return bookTab() ? BOOK_ROWS[index] : GLOBAL_ROWS[index];
  constexpr Row extra[] = {
      Row::RecordedWeek,  Row::RecordedMonth, Row::RecordedYear, Row::BackupDevice,
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
      Row::RestoreDevice, Row::ResetDevice,
#endif
  };
  if (tab == Tab::Device && index < basic + static_cast<int>(std::size(extra))) {
    return extra[index - basic];
  }
  constexpr int allFrequencyCount = 3;
  if (tab == Tab::AllDevices && index < basic + allFrequencyCount) return extra[index - basic];
  return static_cast<Row>(static_cast<int>(Row::Morning) + index - basic -
                          (tab == Tab::Device       ? static_cast<int>(std::size(extra))
                           : tab == Tab::AllDevices ? allFrequencyCount
                                                    : 0));
}

void ReadingStatsActivity::provideRow(void* context, uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<ReadingStatsActivity*>(context);
  if (index >= self->listCount()) return;
  item = {};
  self->formatRow(self->rowAt(index), item);
  item.actionValue = index;
}

void ReadingStatsActivity::formatRow(Row row, fui::ListItem& item) {
  const bool perBook = bookTab();
  const bool all = tab == Tab::AllDevices;
  const auto& global = all ? aggregate.stats : local;
  const bool available = perBook ? book.writable : global.writable;
  const uint32_t seconds = perBook ? book.totalReadingSeconds : global.totalReadingSeconds;
  const uint32_t sessions = perBook ? book.sessionCount : global.totalSessions;
  const uint32_t pages = perBook ? book.totalPagesTurned : global.totalPagesTurned;
  const auto number = [this](uint32_t n) { snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(n)); };
  const auto duration = [this](uint32_t n) { BookReadingStats::formatDuration(n, value, sizeof(value)); };
  const auto date = [this](const ReadingStatsDate& d) {
    if (d.isValid())
      snprintf(value, sizeof(value), "%04d-%02d-%02d", d.year, d.month, d.day);
    else
      snprintf(value, sizeof(value), "-");
  };
  snprintf(value, sizeof(value), "-");
  StrId label = StrId::STR_STATS_DATA_STATE;
  if (row >= Row::Morning) {
    static constexpr StrId labels[] = {StrId::STR_STATS_MORNING,   StrId::STR_STATS_AFTERNOON, StrId::STR_STATS_EVENING,
                                       StrId::STR_STATS_NIGHT,     StrId::STR_STATS_MONDAY,    StrId::STR_STATS_TUESDAY,
                                       StrId::STR_STATS_WEDNESDAY, StrId::STR_STATS_THURSDAY,  StrId::STR_STATS_FRIDAY,
                                       StrId::STR_STATS_SATURDAY,  StrId::STR_STATS_SUNDAY};
    const unsigned index = static_cast<unsigned>(row) - static_cast<unsigned>(Row::Morning);
    label = labels[index];
    if (available) {
      const auto& times = perBook ? book.timeOfDaySeconds : global.timeOfDaySeconds;
      const auto& days = perBook ? book.dayOfWeekSeconds : global.dayOfWeekSeconds;
      duration(index < times.size() ? times[index] : days[index - times.size()]);
    }
  } else {
    switch (row) {
      case Row::Title:
        label = StrId::STR_STATS_BOOK;
        break;
      case Row::Progress:
        label = StrId::STR_STATS_PROGRESS;
        if (progress >= 0) snprintf(value, sizeof(value), "%d.%02d%%", progress / 100, progress % 100);
        break;
      case Row::Time:
        label = StrId::STR_STATS_TOTAL_TIME;
        if (available) duration(seconds);
        break;
      case Row::Sessions:
        label = StrId::STR_STATS_SESSIONS;
        if (available) number(sessions);
        break;
      case Row::AverageSession:
        label = StrId::STR_STATS_AVERAGE_SESSION;
        if (available && sessions) duration(seconds / sessions);
        break;
      case Row::Pages:
        label = StrId::STR_STATS_PAGE_TURNS;
        if (available) number(pages);
        break;
      case Row::PagesPerMinute:
        label = StrId::STR_STATS_PAGES_PER_MINUTE;
        if (available && seconds > 60) {
          const uint64_t tenths = static_cast<uint64_t>(pages) * 600 / seconds;
          snprintf(value, sizeof(value), "%llu.%llu", static_cast<unsigned long long>(tenths / 10),
                   static_cast<unsigned long long>(tenths % 10));
        }
        break;
      case Row::TimeLeft:
        label = StrId::STR_BAR_TIME_BOOK;
        if (const auto remaining = book.remainingReadingSeconds(progress)) duration(remaining);
        break;
      case Row::Started:
        label = StrId::STR_STATS_STARTED;
        if (closedDateSavePending && dateResultStarted)
          snprintf(value, sizeof(value), "%s", tr(STR_STATS_BACKUP_SAVING));
        else if (invalidDate && dateResultStarted)
          snprintf(value, sizeof(value), "%s", tr(STR_STATS_INVALID_DATE));
        else if (dateSaveIncomplete && dateResultStarted)
          snprintf(value, sizeof(value), "%s",
                   liveStats ? tr(STR_STATS_SAVE_INCOMPLETE) : tr(STR_STATS_CLOSED_DATE_CHECK));
        else if (available)
          date(book.startDate);
        break;
      case Row::Finished:
        label = StrId::STR_STATS_FINISHED;
        if (closedDateSavePending && !dateResultStarted)
          snprintf(value, sizeof(value), "%s", tr(STR_STATS_BACKUP_SAVING));
        else if (invalidDate && !dateResultStarted)
          snprintf(value, sizeof(value), "%s", tr(STR_STATS_INVALID_DATE));
        else if (dateSaveIncomplete && !dateResultStarted)
          snprintf(value, sizeof(value), "%s",
                   liveStats ? tr(STR_STATS_SAVE_INCOMPLETE) : tr(STR_STATS_CLOSED_DATE_CHECK));
        else if (available)
          date(book.finishedDate);
        break;
      case Row::DaysReading: {
        label = StrId::STR_STATS_DAYS_READING;
        const auto end = book.isCompleted && book.finishedDate.isValid() ? book.finishedDate : now.date;
        if (available && book.startDate.isValid() && end.isValid()) number(readingSpanDaysElapsed(book.startDate, end));
        break;
      }
      case Row::ProjectedFinish:
        label = StrId::STR_STATS_PROJECTED_FINISH;
        if (available) date(book.projectedFinishDate(now, progress));
        break;
      case Row::Completion:
        label = StrId::STR_STATS_COMPLETION;
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
        if (completionRecoveryChecking)
          snprintf(value, sizeof(value), "%s", tr(STR_STATS_TRACKING_CHECKING));
        else if (completionPending)
          snprintf(value, sizeof(value), "%s", tr(STR_STATS_BACKUP_SAVING));
        else if (completionFailed)
          snprintf(value, sizeof(value), "%s", tr(STR_RETRY));
        else
#endif
            if (available)
          snprintf(value, sizeof(value), "%s", book.isCompleted ? tr(STR_STATS_FINISHED) : tr(STR_STATS_IN_PROGRESS));
        break;
      case Row::Tracking:
        label = StrId::STR_STATS_TRACKING;
        snprintf(value, sizeof(value), "%s",
                 trackingStatus == TrackingStatus::On
                     ? (trackingAppliesOnReturn ? tr(STR_ENABLED) : tr(STR_STATS_TRACKING_NEXT_ON))
                 : trackingStatus == TrackingStatus::Off
                     ? (trackingAppliesOnReturn ? tr(STR_DISABLED) : tr(STR_STATS_TRACKING_NEXT_OFF))
                 : trackingStatus == TrackingStatus::Failed   ? tr(STR_STATS_TRACKING_CHECK_AGAIN)
                 : trackingStatus == TrackingStatus::Checking ? tr(STR_STATS_TRACKING_CHECKING)
                                                              : tr(STR_STATS_BACKUP_SAVING));
        break;
      case Row::CompletedBooks:
        label = StrId::STR_STATS_COMPLETED;
        if (available) number(global.completedBooks);
        break;
      case Row::CurrentStreak:
        label = StrId::STR_STATS_CURRENT_STREAK;
        if (available && now.date.isValid()) number(global.currentReadingStreak(&now.date));
        break;
      case Row::LongestStreak:
        label = StrId::STR_STATS_LONGEST_STREAK;
        if (available) number(global.displayLongestReadingStreak());
        break;
      case Row::RecordedWeek:
      case Row::RecordedMonth:
      case Row::RecordedYear: {
        label = row == Row::RecordedWeek    ? StrId::STR_STATS_RECORDED_WEEK
                : row == Row::RecordedMonth ? StrId::STR_STATS_RECORDED_MONTH
                                            : StrId::STR_STATS_RECORDED_YEAR;
        const uint16_t days = row == Row::RecordedWeek ? 7 : row == Row::RecordedMonth ? 30 : 365;
        if (available && readingHistoryCoversWindow(global.readingHistoryAnchorDay, now.date, days))
          number(recordedReadingDays(global.readingHistoryAnchorDay, global.readingHistoryBits, now.date, days));
        break;
      }
      case Row::Sources:
        label = StrId::STR_STATS_SOURCES;
        if (available) number(all ? aggregate.sources : 1);
        break;
      case Row::Skipped:
        label = StrId::STR_STATS_SKIPPED;
        if (available) number(all ? aggregate.skipped : 0);
        break;
      case Row::State:
        label = StrId::STR_STATS_DATA_STATE;
        snprintf(value, sizeof(value), "%s",
                 !available                             ? tr(STR_STATS_DATA_UNAVAILABLE)
                 : invalidDate && perBook               ? tr(STR_STATS_INVALID_DATE)
                 : !saved && all && !aggregate.complete ? tr(STR_STATS_SAVE_PARTIAL)
                 : !saved                               ? tr(STR_STATS_SAVE_INCOMPLETE)
                 : all && !aggregate.complete           ? tr(STR_STATS_PARTIAL)
                                                        : tr(STR_OK_BUTTON));
        break;
      case Row::Charts:
        label = StrId::STR_STATS_CHARTS;
        break;
      case Row::BackupDevice:
      case Row::BackupBook: {
        label = row == Row::BackupBook ? StrId::STR_STATS_BACKUP_BOOK : StrId::STR_STATS_BACKUP_DEVICE;
        const auto status = row == Row::BackupBook ? bookBackupStatus : backupStatus;
        if (status != BackupStatus::None) {
          const auto state = status == BackupStatus::Saving        ? StrId::STR_STATS_BACKUP_SAVING
                             : status == BackupStatus::Saved       ? StrId::STR_STATS_BACKUP_SAVED
                             : status == BackupStatus::Unavailable ? StrId::STR_STATS_BACKUP_UNAVAILABLE
                                                                   : StrId::STR_STATS_BACKUP_FAILED;
          snprintf(value, sizeof(value), "%s", I18N.get(state));
        }
        break;
      }
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
      case Row::RestoreDevice:
        label = StrId::STR_STATS_RESTORE_DEVICE;
        if (restoreStatus != BackupStatus::None)
          snprintf(value, sizeof(value), "%s",
                   I18N.get(restoreStatus == BackupStatus::Saved         ? StrId::STR_STATS_RESTORE_DONE
                            : restoreStatus == BackupStatus::Saving      ? StrId::STR_STATS_BACKUP_SAVING
                            : restoreStatus == BackupStatus::Unavailable ? StrId::STR_STATS_RESTORE_UNCERTAIN
                                                                         : StrId::STR_STATS_RESTORE_FAILED));
        break;
      case Row::RestoreBook:
        label = StrId::STR_STATS_RESTORE_BOOK;
        if (liveStats || !closedBookDateEdit)
          snprintf(value, sizeof(value), "%s", tr(STR_STATS_RESTORE_CLOSE_BOOK));
        else if (bookRestoreRecovery == BookRestoreRecovery::Available)
          snprintf(value, sizeof(value), "%s", tr(STR_STATS_RESTORE_RESUME));
        else if (bookRestoreRecovery == BookRestoreRecovery::Checking ||
                 bookRestoreRecovery == BookRestoreRecovery::Running)
          snprintf(value, sizeof(value), "%s", tr(STR_STATS_RESTORE_CHECKING));
        else if (bookRestoreRecovery == BookRestoreRecovery::ManualReview)
          snprintf(value, sizeof(value), "%s", tr(STR_STATS_RESTORE_UNCERTAIN));
        else if (bookRestoreStatus != BackupStatus::None)
          snprintf(value, sizeof(value), "%s",
                   I18N.get(bookRestoreStatus == BackupStatus::Saved         ? StrId::STR_STATS_RESTORE_DONE
                            : bookRestoreStatus == BackupStatus::Saving      ? StrId::STR_STATS_BACKUP_SAVING
                            : bookRestoreStatus == BackupStatus::Unavailable ? StrId::STR_STATS_RESTORE_UNCERTAIN
                                                                             : StrId::STR_STATS_RESTORE_FAILED));
        break;
      case Row::ResetDevice:
        label = StrId::STR_STATS_RESET_DEVICE;
        if (resetStatus != BackupStatus::None)
          snprintf(value, sizeof(value), "%s",
                   I18N.get(resetStatus == BackupStatus::Saved         ? StrId::STR_STATS_RESET_DONE
                            : resetStatus == BackupStatus::Saving      ? StrId::STR_STATS_BACKUP_SAVING
                            : resetStatus == BackupStatus::Unavailable ? StrId::STR_STATS_RESTORE_UNCERTAIN
                                                                       : StrId::STR_STATS_RESET_FAILED));
        break;
#endif
      default:
        break;
    }
  }
  item.label = row == Row::Title ? bookTitle : I18N.get(label);
  item.value = row == Row::Title || row == Row::Charts ||
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
                       (row == Row::RestoreDevice && restoreStatus == BackupStatus::None) ||
                       (row == Row::ResetDevice && resetStatus == BackupStatus::None) ||
                       (row == Row::RestoreBook && bookRestoreStatus == BackupStatus::None && !liveStats &&
                        closedBookDateEdit && bookRestoreRecovery == BookRestoreRecovery::None) ||
#endif
                       (row == Row::BackupDevice && backupStatus == BackupStatus::None) ||
                       (row == Row::BackupBook && bookBackupStatus == BackupStatus::None)
                   ? nullptr
                   : value;
}

void ReadingStatsActivity::activateIndex(int index) {
  if (index < 0 || index >= listCount()) return;
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
  if (rowAt(index) == Row::ResetDevice) {
    RenderLock lock(*this);
    app.clearTapFlash();
    confirmDeviceReset();
    return;
  }
  if (rowAt(index) == Row::RestoreBook) {
    RenderLock lock(*this);
    app.clearTapFlash();
    if (!liveStats && closedBookDateEdit && bookRestoreRecovery == BookRestoreRecovery::Available) {
      bookRestoreRecovery = BookRestoreRecovery::Running;
      requestUpdate();
    } else if (!liveStats && closedBookDateEdit && bookRestoreRecovery == BookRestoreRecovery::None) {
      pickBookBackup();
    }
    return;
  }
  if (rowAt(index) == Row::RestoreDevice) {
    RenderLock lock(*this);
    app.clearTapFlash();
    pickDeviceBackup();
    return;
  }
#endif
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
  if (bookTab() && rowAt(index) == Row::Completion && closedBookDateEdit && !liveStats && !completionRecoveryChecking &&
      !completionPending && (completionFailed || (book.writable && saved))) {
    RenderLock lock(*this);
    app.clearTapFlash();
    completionRetryRecovery = completionFailed;
    if (!completionFailed) completionDesired = !book.isCompleted;
    completionFailed = false;
    completionPending = true;
    requestUpdate();
    return;
  }
#endif
  if (bookTab() && (liveStats || (closedBookDateEdit && book.writable)) &&
      (rowAt(index) == Row::Started || rowAt(index) == Row::Finished)) {
    RenderLock lock(*this);
    app.clearTapFlash();
    editDate(rowAt(index) == Row::Started);
    return;
  }
  if (rowAt(index) == Row::Tracking) {
    RenderLock lock(*this);
    app.clearTapFlash();
    trackingStatus = trackingStatus == TrackingStatus::On    ? TrackingStatus::SavingOff
                     : trackingStatus == TrackingStatus::Off ? TrackingStatus::SavingOn
                                                             : TrackingStatus::Checking;
    requestUpdate();
    return;
  }
  if (rowAt(index) == Row::BackupBook) {
    RenderLock lock(*this);
    app.clearTapFlash();
    bookBackupStatus = !saved || !book.writable ? BackupStatus::Unavailable : BackupStatus::Saving;
    requestUpdate();
    return;
  }
  if (rowAt(index) != Row::BackupDevice) {
    RenderLock lock(*this);
    app.clearTapFlash();
    if (rowAt(index) == Row::Charts) openCharts();
    return;
  }
  RenderLock lock(*this);
  app.clearTapFlash();
  backupStatus = !saved || !local.writable ? BackupStatus::Unavailable : BackupStatus::Saving;
  requestUpdate();
}

bool ReadingStatsActivity::applyRequestedTrackingToggle() {
  if (!pendingTrackingToggle || trackingStatus == TrackingStatus::Checking) return false;
  RenderLock lock(*this);
  pendingTrackingToggle = false;
  tab = Tab::Book;
  for (size_t i = 0; i < sizeof(BOOK_ROWS) / sizeof(BOOK_ROWS[0]); ++i) {
    if (BOOK_ROWS[i] == Row::Tracking) {
      moveRingTo(static_cast<int>(i) + 1);
      break;
    }
  }
  if (trackingStatus == TrackingStatus::On)
    trackingStatus = TrackingStatus::SavingOff;
  else if (trackingStatus == TrackingStatus::Off)
    trackingStatus = TrackingStatus::SavingOn;
  requestUpdate();
  return true;
}

void ReadingStatsActivity::loop() {
  if (applyRequestedTrackingToggle()) return;
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
  if (resetPending) {
    requestUpdateAndWait();
    const auto result = statsbackup::resetDeviceStats(restoreCurrentPreview.data(), restoreCurrentPreview.size());
    const auto updated = GlobalReadingStats::loadReadOnly();
    const auto updatedAggregate = GlobalReadingStats::loadAggregated(updated);
    RenderLock lock(*this);
    local = updated;
    aggregate = updatedAggregate;
    resetPending = false;
    resetStatus = result == statsbackup::RestoreResult::Restored         ? BackupStatus::Saved
                  : result == statsbackup::RestoreResult::WriteUncertain ? BackupStatus::Unavailable
                                                                         : BackupStatus::Failed;
    if (result == statsbackup::RestoreResult::WriteUncertain) statsSaveNotice().recordFailure();
    requestUpdate();
    return;
  }
  if (bookRestoreRecovery == BookRestoreRecovery::Checking
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
      && !completionRecoveryChecking
#endif
  ) {
    bool target = false;
    uint8_t policy = 0;
    const auto probe = readingstats::probeClosedBookCompletion(trackingBookPath, target, &policy);
    RenderLock lock(*this);
    bookRestoreRecoveryTarget = target;
    bookRestoreRecovery = probe == readingstats::CompletionProbeResult::Matching && policy == 0
                              ? BookRestoreRecovery::Available
                              : BookRestoreRecovery::ManualReview;
    requestUpdate();
    return;
  }
  if (bookRestoreRecovery == BookRestoreRecovery::Running) {
    requestUpdateAndWait();
    const auto recovered = readingstats::recoverClosedBookCompletionStats(trackingBookPath, bookRestoreRecoveryTarget);
    const bool finished = recovered == readingstats::CompletionRecoveryResult::Recovered &&
                          readingstats::finalizeClosedBookCompletion(trackingBookPath, bookRestoreRecoveryTarget) ==
                              readingstats::CompletionFinalizeResult::Finalized;
    bool enabled = false, found = false, known = false;
    auto updated = readingstats::loadBook(trackingCachePath, trackingBookPath, enabled, &found,
                                          BookReadingStats::LoadMode::TransactionalEdit, &known);
    const auto updatedGlobal = GlobalReadingStats::loadReadOnly();
    const auto updatedAggregate = GlobalReadingStats::loadAggregated(updatedGlobal);
    RenderLock lock(*this);
    book = updated;
    local = updatedGlobal;
    aggregate = updatedAggregate;
    bookRestoreRecovery = finished ? BookRestoreRecovery::None : BookRestoreRecovery::ManualReview;
    bookRestoreStatus = finished ? BackupStatus::Saved : BackupStatus::Unavailable;
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
    if (finished && found && known && updated.writable && updatedGlobal.writable) {
      completionFailed = false;
      saved = true;
    }
#endif
    if (!finished) statsSaveNotice().recordFailure();
    requestUpdate();
    return;
  }
  if (bookRestorePending) {
    requestUpdateAndWait();
    const auto result = statsbackup::restoreBookBackup(
        trackingCachePath, trackingBookPath, restoreFilename, bookRestoreCurrentPreview.data(),
        bookRestoreCurrentPreview.size(), bookRestorePreview.data(), bookRestorePreview.size());
    bool enabled = false, found = false, known = false;
    auto updated = readingstats::loadBook(trackingCachePath, trackingBookPath, enabled, &found,
                                          BookReadingStats::LoadMode::TransactionalEdit, &known);
    const auto updatedGlobal = GlobalReadingStats::loadReadOnly();
    const auto updatedAggregate = GlobalReadingStats::loadAggregated(updatedGlobal);
    RenderLock lock(*this);
    book = updated;
    local = updatedGlobal;
    aggregate = updatedAggregate;
    bookRestorePending = false;
    if (result == statsbackup::BookRestoreResult::RecoveryRequired)
      bookRestoreRecovery = BookRestoreRecovery::Checking;
    else if (result == statsbackup::BookRestoreResult::WriteUncertain)
      bookRestoreRecovery = BookRestoreRecovery::ManualReview;
    bookRestoreStatus = result == statsbackup::BookRestoreResult::Restored ? BackupStatus::Saved
                        : result == statsbackup::BookRestoreResult::WriteUncertain ||
                                result == statsbackup::BookRestoreResult::RecoveryRequired
                            ? BackupStatus::Unavailable
                            : BackupStatus::Failed;
    if (bookRestoreStatus == BackupStatus::Unavailable) statsSaveNotice().recordFailure();
    requestUpdate();
    return;
  }
  if (restorePending) {
    requestUpdateAndWait();
    GlobalReadingStats selected;
    std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> encoded{};
    auto result = statsbackup::RestoreResult::Unavailable;
    if (statsbackup::readDeviceBackup(restoreFilename, selected) && selected.encode(encoded) &&
        encoded == restorePreview)
      result =
          statsbackup::restoreDeviceBackup(restoreFilename, restoreCurrentPreview.data(), restoreCurrentPreview.size(),
                                           restorePreview.data(), restorePreview.size());
    GlobalReadingStats updated = GlobalReadingStats::loadReadOnly();
    GlobalStatsAggregate updatedAggregate = GlobalReadingStats::loadAggregated(updated);
    RenderLock lock(*this);
    local = updated;
    aggregate = updatedAggregate;
    restorePending = false;
    restoreStatus = result == statsbackup::RestoreResult::Restored         ? BackupStatus::Saved
                    : result == statsbackup::RestoreResult::WriteUncertain ? BackupStatus::Unavailable
                                                                           : BackupStatus::Failed;
    if (result == statsbackup::RestoreResult::WriteUncertain) statsSaveNotice().recordFailure();
    requestUpdate();
    return;
  }
#endif
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
  if (completionRecoveryChecking) {
    bool target = false;
    uint8_t policy = 0;
    const auto result = readingstats::probeClosedBookCompletion(trackingBookPath, target, &policy);
    RenderLock lock(*this);
    completionRecoveryChecking = false;
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
    if (bookRestoreRecovery == BookRestoreRecovery::Checking) {
      bookRestoreRecoveryTarget = target;
      bookRestoreRecovery = result == readingstats::CompletionProbeResult::Matching && policy == 0
                                ? BookRestoreRecovery::Available
                                : BookRestoreRecovery::ManualReview;
    }
#endif
    if (result == readingstats::CompletionProbeResult::Matching) {
      completionDesired = target;
      completionFailed = true;
      saved = false;
    } else if (result == readingstats::CompletionProbeResult::ManualReview) {
      saved = false;
      statsSaveNotice().recordFailure();
    }
    requestUpdate();
    return;
  }
  if (completionPending) {
    requestUpdateAndWait();
    struct CompletionSnapshot {
      BookReadingStats book;
      GlobalReadingStats global;
      GlobalStatsAggregate aggregate;
    };
    auto snapshot = makeUniqueNoThrow<CompletionSnapshot>();
    ReadingStatsDateTime current{};
    getCurrentLocalReadingStatsDateTime(current);
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION_MOVE)
    std::string movedPath;
    const auto result = snapshot ? attemptCompletion(trackingBookPath, completionDesired, current.date,
                                                     completionRetryRecovery, &movedPath)
                                 : readingstats::ClosedCompletionResult::Unavailable;
#else
    const auto result =
        snapshot ? attemptCompletion(trackingBookPath, completionDesired, current.date, completionRetryRecovery)
                 : readingstats::ClosedCompletionResult::Unavailable;
#endif
    const bool committed = result == readingstats::ClosedCompletionResult::Applied ||
                           result == readingstats::ClosedCompletionResult::Unchanged;
    bool enabled = false, found = false, known = false;
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION_MOVE)
    const std::string movedCache =
        movedPath.empty() ? std::string{} : Epub::cachePathForFilePath(movedPath, "/.crosspoint");
#endif
    if (committed) {
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION_MOVE)
      snapshot->book = readingstats::loadBook(movedPath.empty() ? trackingCachePath : movedCache,
                                              movedPath.empty() ? trackingBookPath : movedPath, enabled, &found,
                                              BookReadingStats::LoadMode::ReadOnly, &known);
#else
      snapshot->book = readingstats::loadBook(trackingCachePath, trackingBookPath, enabled, &found,
                                              BookReadingStats::LoadMode::ReadOnly, &known);
#endif
      snapshot->global = GlobalReadingStats::loadReadOnly();
      snapshot->aggregate = GlobalReadingStats::loadAggregated(snapshot->global);
    }
    RenderLock lock(*this);
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION_MOVE)
    if (!movedPath.empty()) {
      trackingBookPath = std::move(movedPath);
      trackingCachePath = movedCache;
    }
#endif
    completionPending = false;
    completionFailed = !committed || !found || !known || !snapshot->book.writable || !snapshot->global.writable;
    if (!completionFailed) {
      book = snapshot->book;
      local = snapshot->global;
      aggregate = snapshot->aggregate;
      saved = true;
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
      bookRestoreRecovery = BookRestoreRecovery::None;
#endif
    } else {
      saved = false;
      statsSaveNotice().recordFailure();
    }
    requestUpdate();
    return;
  }
#endif
  if (closedDateSavePending) {
    requestUpdateAndWait();
    BookReadingStats updated;
    const bool succeeded = readingstats::editClosedBookDate(trackingCachePath, trackingBookPath, dateResultStarted,
                                                            pendingClosedDate, updated);
    RenderLock lock(*this);
    if (succeeded) book = updated;
    saved = succeeded;
    dateSaveIncomplete = !succeeded;
    closedDateSavePending = false;
    requestUpdate();
    return;
  }
  if (hasBook && (trackingStatus == TrackingStatus::Checking || trackingStatus == TrackingStatus::SavingOn ||
                  trackingStatus == TrackingStatus::SavingOff)) {
    requestUpdateAndWait();
    const auto requested = trackingStatus;
    const bool succeeded =
        requested == TrackingStatus::Checking ||
        booktracking::setEnabled(trackingCachePath, trackingBookPath, requested == TrackingStatus::SavingOn);
    const auto state =
        succeeded ? booktracking::state(trackingCachePath, trackingBookPath) : booktracking::State::Unknown;
    RenderLock lock(*this);
    trackingStatus = state == booktracking::State::Enabled    ? TrackingStatus::On
                     : state == booktracking::State::Disabled ? TrackingStatus::Off
                                                              : TrackingStatus::Failed;
    requestUpdate();
    return;
  }
  if (bookBackupStatus == BackupStatus::Saving) {
    requestUpdateAndWait();
    const bool succeeded = statsbackup::createBookBackup(trackingCachePath, book);
    RenderLock lock(*this);
    bookBackupStatus = succeeded ? BackupStatus::Saved : BackupStatus::Failed;
    requestUpdate();
    return;
  }
  if (backupStatus != BackupStatus::Saving) {
    UiTabListActivity::loop();
    return;
  }
  // Touch dispatch must finish before this repaint; otherwise it can replace
  // the pending event before the normal touch feedback is delivered.
  requestUpdateAndWait();
  const bool succeeded = statsbackup::createDeviceBackup(local);
  RenderLock lock(*this);
  backupStatus = succeeded ? BackupStatus::Saved : BackupStatus::Failed;
  requestUpdate();
}

#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
void ReadingStatsActivity::savePendingCompletionBeforeExit() {
  if (!completionPending) return;
  ReadingStatsDateTime current{};
  getCurrentLocalReadingStatsDateTime(current);
  const auto result =
      attemptCompletion(trackingBookPath, completionDesired, current.date, completionRetryRecovery, nullptr, false);
  if (result != readingstats::ClosedCompletionResult::Applied &&
      result != readingstats::ClosedCompletionResult::Unchanged)
    statsSaveNotice().recordFailure();
  completionPending = false;
}
#endif

void ReadingStatsActivity::editDate(bool started) {
  const auto& current = started ? book.startDate : book.finishedDate;
  char initial[11]{};
  if (current.isValid())
    snprintf(initial, sizeof(initial), "%04u-%02u-%02u", static_cast<unsigned>(current.year),
             static_cast<unsigned>(current.month), static_cast<unsigned>(current.day));
  auto editor = makeUniqueNoThrow<KeyboardEntryActivity>(
      renderer, mappedInput, started ? tr(STR_STATS_EDIT_STARTED) : tr(STR_STATS_EDIT_FINISHED), initial, 10,
      InputType::Text);
  if (!editor) return;
  startActivityForResult(std::move(editor), [this, started](const ActivityResult& result) {
    if (result.isCancelled) return;
    ReadingStatsDate date;
    const auto* value = std::get_if<KeyboardResult>(&result.data);
    RenderLock lock(*this);
    dateResultStarted = started;
    if (!value || !parseReadingStatsDateEdit(value->text, date)) {
      invalidDate = true;
    } else {
      invalidDate = false;
      if (liveStats) {
        if (!liveStats->editBookDate(started, date, book, local, saved)) saved = false;
        dateSaveIncomplete = !saved;
      } else {
        pendingClosedDate = date;
        closedDateSavePending = true;
        dateSaveIncomplete = false;
      }
    }
    requestUpdate();
  });
}

void ReadingStatsActivity::openCharts() {
  const bool perBook = bookTab();
  const auto& global = tab == Tab::AllDevices ? aggregate.stats : local;
  readingcharts::Data data;
  data.scope = perBook                  ? readingcharts::Scope::Book
               : tab == Tab::AllDevices ? readingcharts::Scope::AllDevices
                                        : readingcharts::Scope::Device;
  data.available = perBook ? book.writable : global.writable;
  data.timeOfDay = perBook ? book.timeOfDaySeconds : global.timeOfDaySeconds;
  data.weekdays = perBook ? book.dayOfWeekSeconds : global.dayOfWeekSeconds;
  if (!perBook) {
    data.history = global.readingHistoryBits;
    data.anchor = global.readingHistoryAnchorDay;
  }
  data.today = now.date;
  data.warning = !saved && tab == Tab::AllDevices && !aggregate.complete ? readingcharts::Warning::UnsavedPartial
                 : !saved                                                ? readingcharts::Warning::Unsaved
                 : tab == Tab::AllDevices && !aggregate.complete         ? readingcharts::Warning::Partial
                                                                         : readingcharts::Warning::None;
  auto screen = makeUniqueNoThrow<ReadingChartsActivity>(renderer, mappedInput, data,
                                                         perBook ? bookTitle : tabLabel(activeTab()));
  if (!screen) {
    LOG_ERR("STATS", "Insufficient memory for reading charts");
    return;
  }
  startActivityForResult(std::move(screen), [](const ActivityResult&) {});
}

#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
void ReadingStatsActivity::confirmDeviceReset() {
  const auto current = GlobalReadingStats::loadReadOnly();
  std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> preview{};
  if (!current.writable || !current.encode(preview)) {
    resetStatus = BackupStatus::Failed;
    requestUpdate();
    return;
  }
  auto confirm =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_STATS_RESET_DEVICE),
                                              tr(STR_STATS_RESET_CONFIRM), tr(STR_CANCEL), tr(STR_STATS_RESET_ACTION));
  if (!confirm) {
    resetStatus = BackupStatus::Failed;
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(confirm), [this, preview](const ActivityResult& result) {
    if (result.isCancelled) return;
    RenderLock lock(*this);
    restoreCurrentPreview = preview;
    resetStatus = BackupStatus::Saving;
    resetPending = true;
    requestUpdate();
  });
}

void ReadingStatsActivity::pickBookBackup() {
  const size_t slash = trackingCachePath.find_last_of('/');
  const auto leaf = trackingCachePath.substr(slash == std::string::npos ? 0 : slash + 1);
  if (leaf.empty()) {
    bookRestoreStatus = BackupStatus::Failed;
    requestUpdate();
    return;
  }
  const std::string prefix = "/.crossink-stats-backup/books/" + leaf + "/";
  auto picker = makeUniqueNoThrow<FileBrowserActivity>(renderer, mappedInput, prefix.substr(0, prefix.size() - 1),
                                                       FileBrowserActivity::Mode::PickStatsBackup);
  if (!picker) {
    bookRestoreStatus = BackupStatus::Failed;
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(picker), [this, prefix](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* path = std::get_if<FilePathResult>(&result.data);
    if (!path || !path->path.starts_with(prefix)) {
      RenderLock lock(*this);
      bookRestoreStatus = BackupStatus::Failed;
      requestUpdate();
      return;
    }
    confirmBookRestore(path->path.substr(prefix.size()));
  });
}

void ReadingStatsActivity::confirmBookRestore(const std::string& filename) {
  BookReadingStats selected;
  bool enabled = false, found = false, known = false;
  const auto current = readingstats::loadBook(trackingCachePath, trackingBookPath, enabled, &found,
                                              BookReadingStats::LoadMode::TransactionalEdit, &known);
  std::array<uint8_t, BookReadingStats::CURRENT_FILE_SIZE> preview{}, currentPreview{};
  if (!found || !known || !current.writable || !current.encode(currentPreview.data(), currentPreview.size()) ||
      !statsbackup::readBookBackup(trackingCachePath, filename, selected) ||
      !selected.encode(preview.data(), preview.size())) {
    RenderLock lock(*this);
    bookRestoreStatus = BackupStatus::Failed;
    requestUpdate();
    return;
  }
  char summary[160];
  snprintf(summary, sizeof(summary), tr(STR_STATS_RESTORE_SUMMARY),
           static_cast<unsigned long>(current.totalReadingSeconds), static_cast<unsigned long>(current.sessionCount),
           static_cast<unsigned long>(selected.totalReadingSeconds), static_cast<unsigned long>(selected.sessionCount));
  char completion[96];
  snprintf(completion, sizeof(completion), tr(STR_STATS_RESTORE_FINISHED_SUMMARY),
           current.isCompleted ? tr(STR_YES) : tr(STR_NO), selected.isCompleted ? tr(STR_YES) : tr(STR_NO));
  std::string body = std::string(tr(STR_STATS_RESTORE_BOOK_CONFIRM)) + "\n" + summary + "\n" + completion;
  if (current.isCompleted != selected.isCompleted) body += std::string("\n") + tr(STR_STATS_RESTORE_FINISHED_NOTE);
  auto confirm = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_STATS_RESTORE_BOOK), body,
                                                         tr(STR_CANCEL), tr(STR_STATS_RESTORE_ACTION));
  if (!confirm) {
    RenderLock lock(*this);
    bookRestoreStatus = BackupStatus::Failed;
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(confirm), [this, filename, preview, currentPreview](const ActivityResult& result) {
    if (result.isCancelled) return;
    RenderLock lock(*this);
    restoreFilename = filename;
    bookRestorePreview = preview;
    bookRestoreCurrentPreview = currentPreview;
    bookRestoreStatus = BackupStatus::Saving;
    bookRestorePending = true;
    requestUpdate();
  });
}

void ReadingStatsActivity::pickDeviceBackup() {
  auto picker = makeUniqueNoThrow<FileBrowserActivity>(renderer, mappedInput, "/.crossink-stats-backup",
                                                       FileBrowserActivity::Mode::PickStatsBackup);
  if (!picker) {
    restoreStatus = BackupStatus::Failed;
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* path = std::get_if<FilePathResult>(&result.data);
    constexpr char prefix[] = "/.crossink-stats-backup/";
    if (!path || !path->path.starts_with(prefix)) {
      RenderLock lock(*this);
      restoreStatus = BackupStatus::Failed;
      requestUpdate();
      return;
    }
    confirmDeviceRestore(path->path.substr(sizeof(prefix) - 1));
  });
}

void ReadingStatsActivity::confirmDeviceRestore(const std::string& filename) {
  GlobalReadingStats selected;
  const auto current = GlobalReadingStats::loadReadOnly();
  std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> preview{};
  std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> currentPreview{};
  const auto integrity = statsbackup::inspectDeviceBackup(filename, selected);
  if (!current.writable || !current.encode(currentPreview) || integrity == statsbackup::BackupIntegrity::Unavailable ||
      !selected.encode(preview)) {
    RenderLock lock(*this);
    restoreStatus = BackupStatus::Failed;
    requestUpdate();
    return;
  }
  char summary[160];
  snprintf(summary, sizeof(summary), tr(STR_STATS_RESTORE_SUMMARY),
           static_cast<unsigned long>(current.totalReadingSeconds), static_cast<unsigned long>(current.totalSessions),
           static_cast<unsigned long>(selected.totalReadingSeconds),
           static_cast<unsigned long>(selected.totalSessions));
  std::string body = std::string(tr(STR_STATS_RESTORE_CONFIRM)) + "\n" + summary;
  if (integrity == statsbackup::BackupIntegrity::LegacyUnverified)
    body += std::string("\n") + tr(STR_STATS_RESTORE_LEGACY_NOTE);
  auto confirm = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_STATS_RESTORE_DEVICE), body,
                                                         tr(STR_CANCEL), tr(STR_STATS_RESTORE_ACTION));
  if (!confirm) {
    RenderLock lock(*this);
    restoreStatus = BackupStatus::Failed;
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(confirm), [this, filename, preview, currentPreview](const ActivityResult& result) {
    if (result.isCancelled) return;
    RenderLock lock(*this);
    restoreFilename = filename;
    restorePreview = preview;
    restoreCurrentPreview = currentPreview;
    restoreStatus = BackupStatus::Saving;
    restorePending = true;
    requestUpdate();
  });
}
#endif

void ReadingStatsActivity::onTabAction(int index) {
  if (index < 0 || index >= tabCount()) return;
  RenderLock lock(*this);
  tab = static_cast<Tab>(index + (hasBook ? 0 : 1));
  moveRingTo(0);
  app.clearTapFlash();
}

void ReadingStatsActivity::stepTab(int direction) { onTabAction((activeTab() + direction + tabCount()) % tabCount()); }

bool ReadingStatsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() == 0)
      stepTab(1);
    else
      activateIndex(ringPos() - 1);
    return true;
  }
  return false;
}

void ReadingStatsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  buildTabBar(screen);
  fui::ListProps props;
  props.rowProvider = &ReadingStatsActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = listCount();
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncTabListViewport(screen, props);
  screen.list(props);
}

void ReadingStatsActivity::drawFooter() {
  const char* action =
      ringPos() == 0 ? tr(STR_STATS_NEXT_VIEW)
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
      : ringPos() <= listCount() && bookTab() && rowAt(ringPos() - 1) == Row::Completion && completionFailed
          ? tr(STR_RETRY)
#endif
      : ringPos() <= listCount() &&
              (rowAt(ringPos() - 1) == Row::Charts || rowAt(ringPos() - 1) == Row::BackupDevice ||
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
               rowAt(ringPos() - 1) == Row::ResetDevice || rowAt(ringPos() - 1) == Row::RestoreDevice ||
               (rowAt(ringPos() - 1) == Row::RestoreBook && !liveStats && closedBookDateEdit &&
                (bookRestoreRecovery == BookRestoreRecovery::None ||
                 bookRestoreRecovery == BookRestoreRecovery::Available)) ||
#endif
               rowAt(ringPos() - 1) == Row::BackupBook || rowAt(ringPos() - 1) == Row::Tracking ||
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
               (bookTab() && rowAt(ringPos() - 1) == Row::Completion && closedBookDateEdit && !liveStats &&
                !completionRecoveryChecking && !completionPending && (completionFailed || (book.writable && saved))) ||
#endif
               (bookTab() && (liveStats || (closedBookDateEdit && book.writable)) &&
                (rowAt(ringPos() - 1) == Row::Started || rowAt(ringPos() - 1) == Row::Finished)))
          ? tr(STR_SELECT)
          : "";
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), action, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
