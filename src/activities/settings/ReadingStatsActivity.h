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
                       bool saved, const std::string& cachePath, const std::string& bookPath, ReaderStats* liveStats);
  void onEnter() override;
  void loop() override;

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
    Sources,
    Skipped,
    State,
    Charts,
    BackupDevice,
    BackupBook,
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
      Row::Title,          Row::Progress, Row::Time,       Row::Sessions, Row::AverageSession, Row::Pages,
      Row::PagesPerMinute, Row::TimeLeft, Row::Started,    Row::Finished, Row::DaysReading,    Row::ProjectedFinish,
      Row::Completion,     Row::Tracking, Row::BackupBook, Row::State,    Row::Charts};
  inline static constexpr Row GLOBAL_ROWS[] = {
      Row::Time,          Row::Sessions, Row::AverageSession, Row::Pages, Row::CompletedBooks, Row::CurrentStreak,
      Row::LongestStreak, Row::Sources,  Row::Skipped,        Row::State, Row::Charts};
  static void provideRow(void* context, uint16_t index, freeink::ui::ListItem& item);
  void formatRow(Row row, freeink::ui::ListItem& item);
  Row rowAt(int index) const;
  void openCharts();
  void editDate(bool started);
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
  bool invalidDate = false;
  bool dateResultStarted = false;
  bool dateSaveIncomplete = false;
  std::string trackingCachePath;
  std::string trackingBookPath;
  enum class TrackingStatus : uint8_t { Checking, On, Off, SavingOn, SavingOff, Failed };
  TrackingStatus trackingStatus = TrackingStatus::Checking;
  enum class BackupStatus : uint8_t { None, Saving, Saved, Failed, Unavailable };
  BackupStatus backupStatus = BackupStatus::None;
  BackupStatus bookBackupStatus = BackupStatus::None;
  char value[64]{};
};
