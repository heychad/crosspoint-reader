#pragma once

#include <string>

#include "activities/UiTabListActivity.h"
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/GlobalReadingStats.h"

class ReaderStats;

class ReadingStatsActivity final : public UiTabListActivity {
 public:
  ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ReadingStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                       const BookReadingStats& book, const GlobalReadingStats& global, int progress, bool tracking,
                       bool saved, const std::string& cachePath, const std::string& bookPath, ReaderStats* liveStats,
                       bool closedBookDateEdit = false);
  void onEnter() override;
  void onExit() override;
  void prepareForSleep() override;
  void loop() override;
  void requestTrackingToggle() { pendingTrackingToggle = hasBook; }

 private:
  enum class Tab : uint8_t { Book, Device, AllDevices };
  enum class Row : uint8_t {
    Title,
    Progress,
    Time,
    Sessions,
    AverageSession,
    Pages,
    PagesPerMinute,
    TimeLeft,
    Started,
    Finished,
    DaysReading,
    ProjectedFinish,
    Completion,
    Tracking,
    CompletedBooks,
    CurrentStreak,
    LongestStreak,
    RecordedWeek,
    RecordedMonth,
    RecordedYear,
    Sources,
    Skipped,
    State,
    Charts,
    BackupDevice,
    BackupBook,
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
    RestoreDevice,
    RestoreBook,
    ResetDevice,
#endif
    Morning,
    Afternoon,
    Evening,
    Night,
    Monday,
    Tuesday,
    Wednesday,
    Thursday,
    Friday,
    Saturday,
    Sunday
  };
  inline static constexpr Row BOOK_ROWS[] = {
      Row::Title,       Row::Progress,        Row::Time,       Row::Sessions, Row::AverageSession,
      Row::Pages,       Row::PagesPerMinute,  Row::TimeLeft,   Row::Started,  Row::Finished,
      Row::DaysReading, Row::ProjectedFinish, Row::Completion, Row::Tracking, Row::BackupBook,
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
      Row::RestoreBook,
#endif
      Row::State,       Row::Charts};
  inline static constexpr Row GLOBAL_ROWS[] = {Row::Time,          Row::Sessions,       Row::AverageSession,
                                               Row::Pages,         Row::PagesPerMinute, Row::CompletedBooks,
                                               Row::CurrentStreak, Row::LongestStreak,  Row::Sources,
                                               Row::Skipped,       Row::State,          Row::Charts};
  static void provideRow(void* context, uint16_t index, freeink::ui::ListItem& item);
  void formatRow(Row row, freeink::ui::ListItem& item);
  Row rowAt(int index) const;
  void openCharts();
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
  void pickDeviceBackup();
  void confirmDeviceRestore(const std::string& filename);
  void pickBookBackup();
  void confirmBookRestore(const std::string& filename);
  void confirmDeviceReset();
#endif
  void editDate(bool started);
  void savePendingDateBeforeExit();
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
  void savePendingCompletionBeforeExit();
#endif
  bool bookTab() const { return tab == Tab::Book; }
  int listCount() const override;
  int tabCount() const override { return hasBook ? 3 : 2; }
  int activeTab() const override { return static_cast<int>(tab) - (hasBook ? 0 : 1); }
  const char* tabLabel(int index) const override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;
  bool handleButtons() override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void drawFooter() override;

  char bookTitle[160]{};
  BookReadingStats book;
  GlobalReadingStats local;
  GlobalStatsAggregate aggregate;
  ReadingStatsDateTime now;
  int progress = -1;
  Tab tab = Tab::Device;
  bool hasBook = false;
  bool tracking = true;
  bool saved = true;
  ReaderStats* liveStats = nullptr;
  bool closedBookDateEdit = false;
  bool invalidDate = false;
  bool dateResultStarted = false;
  bool dateSaveIncomplete = false;
  bool closedDateSavePending = false;
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
  bool completionPending = false;
  bool completionDesired = false;
  bool completionFailed = false;
  bool completionRetryRecovery = false;
  bool completionRecoveryChecking = false;
#endif
  ReadingStatsDate pendingClosedDate;
  std::string trackingCachePath;
  std::string trackingBookPath;
  enum class TrackingStatus : uint8_t { Checking, On, Off, SavingOn, SavingOff, Failed };
  TrackingStatus trackingStatus = TrackingStatus::Checking;
  bool pendingTrackingToggle = false;
  bool applyRequestedTrackingToggle();
  enum class BackupStatus : uint8_t { None, Saving, Saved, Failed, Unavailable };
  BackupStatus backupStatus = BackupStatus::None;
  BackupStatus bookBackupStatus = BackupStatus::None;
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
  BackupStatus restoreStatus = BackupStatus::None;
  bool restorePending = false;
  bool resetPending = false;
  BackupStatus resetStatus = BackupStatus::None;
  bool bookRestorePending = false;
  BackupStatus bookRestoreStatus = BackupStatus::None;
  enum class BookRestoreRecovery : uint8_t { None, Checking, Available, Running, ManualReview };
  BookRestoreRecovery bookRestoreRecovery = BookRestoreRecovery::None;
  bool bookRestoreRecoveryTarget = false;
  std::string restoreFilename;
  std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> restorePreview{};
  std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE> restoreCurrentPreview{};
  std::array<uint8_t, BookReadingStats::CURRENT_FILE_SIZE> bookRestorePreview{};
  std::array<uint8_t, BookReadingStats::CURRENT_FILE_SIZE> bookRestoreCurrentPreview{};
#endif
  char value[64]{};
};
