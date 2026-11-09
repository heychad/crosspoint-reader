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
#include "activities/reader/DeviceStatsBackup.h"
#include "activities/reader/ReaderStats.h"
#include "activities/reader/ReadingStatsDateEdit.h"
#include "activities/reader/ReadingStatsStore.h"
#include "activities/reader/StatsSaveNotice.h"
#include "activities/settings/ReadingChartsActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

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
  UiTabListActivity::onExit();
}

void ReadingStatsActivity::prepareForSleep() { savePendingDateBeforeExit(); }

const char* ReadingStatsActivity::headerTitle() const { return tr(STR_READING_STATISTICS); }

const char* ReadingStatsActivity::tabLabel(int index) const {
  static constexpr StrId labels[] = {StrId::STR_STATS_BOOK, StrId::STR_STATS_DEVICE, StrId::STR_STATS_ALL_DEVICES};
  return I18N.get(labels[index + (hasBook ? 0 : 1)]);
}

int ReadingStatsActivity::listCount() const {
  return (bookTab() ? std::size(BOOK_ROWS) : std::size(GLOBAL_ROWS)) + (tab == Tab::Device ? 3 : 0) +
         static_cast<int>(Row::Sunday) - static_cast<int>(Row::Morning) + 1;
}

ReadingStatsActivity::Row ReadingStatsActivity::rowAt(int index) const {
  const int basic = bookTab() ? std::size(BOOK_ROWS) : std::size(GLOBAL_ROWS);
  if (index < basic) return bookTab() ? BOOK_ROWS[index] : GLOBAL_ROWS[index];
  if (tab == Tab::Device && index < basic + 3) {
    constexpr Row extra[] = {Row::RecordedWeek, Row::RecordedMonth, Row::BackupDevice};
    return extra[index - basic];
  }
  return static_cast<Row>(static_cast<int>(Row::Morning) + index - basic - (tab == Tab::Device ? 3 : 0));
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
          snprintf(value, sizeof(value), "%s", liveStats ? tr(STR_STATS_SAVE_INCOMPLETE) : tr(STR_STATS_CLOSED_DATE_CHECK));
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
          snprintf(value, sizeof(value), "%s", liveStats ? tr(STR_STATS_SAVE_INCOMPLETE) : tr(STR_STATS_CLOSED_DATE_CHECK));
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
        if (available)
          snprintf(value, sizeof(value), "%s", book.isCompleted ? tr(STR_STATS_FINISHED) : tr(STR_STATS_IN_PROGRESS));
        break;
      case Row::Tracking:
        label = StrId::STR_STATS_TRACKING;
        snprintf(value, sizeof(value), "%s",
                 trackingStatus == TrackingStatus::On         ? tr(STR_STATS_TRACKING_NEXT_ON)
                 : trackingStatus == TrackingStatus::Off      ? tr(STR_STATS_TRACKING_NEXT_OFF)
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
        label = row == Row::RecordedWeek ? StrId::STR_STATS_RECORDED_WEEK : StrId::STR_STATS_RECORDED_MONTH;
        if (available &&
            readingHistoryCoversWindow(global.readingHistoryAnchorDay, now.date, row == Row::RecordedWeek ? 7 : 30))
          number(recordedReadingDays(global.readingHistoryAnchorDay, global.readingHistoryBits, now.date,
                                     row == Row::RecordedWeek ? 7 : 30));
        break;
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
                 !available                   ? tr(STR_STATS_DATA_UNAVAILABLE)
                 : invalidDate && perBook     ? tr(STR_STATS_INVALID_DATE)
                 : !saved                     ? tr(STR_STATS_SAVE_INCOMPLETE)
                 : all && !aggregate.complete ? tr(STR_STATS_PARTIAL)
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
      default:
        break;
    }
  }
  item.label = row == Row::Title ? bookTitle : I18N.get(label);
  item.value = row == Row::Title || row == Row::Charts ||
                       (row == Row::BackupDevice && backupStatus == BackupStatus::None) ||
                       (row == Row::BackupBook && bookBackupStatus == BackupStatus::None)
                   ? nullptr
                   : value;
}

void ReadingStatsActivity::activateIndex(int index) {
  if (index < 0 || index >= listCount()) return;
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

void ReadingStatsActivity::loop() {
  if (closedDateSavePending) {
    requestUpdateAndWait();
    BookReadingStats updated;
    const bool succeeded =
        readingstats::editClosedBookDate(trackingCachePath, trackingBookPath, dateResultStarted, pendingClosedDate, updated);
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
  data.warning = !saved                                          ? readingcharts::Warning::Unsaved
                 : tab == Tab::AllDevices && !aggregate.complete ? readingcharts::Warning::Partial
                                                                 : readingcharts::Warning::None;
  auto screen = makeUniqueNoThrow<ReadingChartsActivity>(renderer, mappedInput, data,
                                                         perBook ? bookTitle : tabLabel(activeTab()));
  if (!screen) {
    LOG_ERR("STATS", "Insufficient memory for reading charts");
    return;
  }
  startActivityForResult(std::move(screen), [](const ActivityResult&) {});
}

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
      : ringPos() <= listCount() && (rowAt(ringPos() - 1) == Row::Charts || rowAt(ringPos() - 1) == Row::BackupDevice ||
                                     rowAt(ringPos() - 1) == Row::BackupBook || rowAt(ringPos() - 1) == Row::Tracking ||
                                     (bookTab() && (liveStats || (closedBookDateEdit && book.writable)) &&
                                      (rowAt(ringPos() - 1) == Row::Started || rowAt(ringPos() - 1) == Row::Finished)))
          ? tr(STR_SELECT)
          : "";
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), action, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
