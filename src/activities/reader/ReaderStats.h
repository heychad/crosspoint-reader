#pragma once

#include <string>

#include "BookStatsBackup.h"
#include "ReadingStatsSession.h"
#include "StatsSessionLease.h"

// Reader-lifetime state; all access is under the existing render/transition lock.
class ReaderStats {
 public:
  void begin(const std::string& cachePath, const std::string& bookPath, bool trackingEnabled, uint16_t idleSeconds,
             bool enableCompletion = false);
  void noteTurn(bool forward, bool succeeded);
  void onRender(ReadingStatsFrame frame);
  void pause();

  // True only when enabled tracking could not safely load its state.
  bool unavailableAtStart() const { return startUnavailable; }
  bool completionStatus(bool& completed) const {
    if (!completionAvailable) return false;
    completed = book.isCompleted;
    return true;
  }
  bool finish();
  // Reconcile a saved per-book toggle after its settings screen closes.
  // A failed flush retains the live session for a later retry.
  bool refreshTracking(const std::string& cachePath, const std::string& bookPath, bool globalTracking,
                       uint16_t idleSeconds, bool enableCompletion = false);
  statsbackup::BookRestoreResult resetBookStatistics(
      const std::string& cachePath, const std::string& bookPath, bool globalTracking, uint16_t idleSeconds,
      bool enableCompletion = false, statsbackup::BookResetScope scope = statsbackup::BookResetScope::Statistics);
  bool snapshot(BookReadingStats& bookOut, GlobalReadingStats& globalOut, bool& saved, bool forceSave = false);
  enum class CompletionResult {
    Applied,
    Unchanged,
    NotTracking,
    Busy,
    SaveFailed,
    RecoveryRequired,
    Unavailable,
    UnsupportedMoveSource
  };
  using RemoveRecent = bool (*)(const std::string& bookPath, void* context);
  // The caller supplies the persisted Recents removal. Moves remain deferred.
  CompletionResult editCompletionStats(const std::string& cachePath, const std::string& bookPath, bool completed,
                                       const ReadingStatsDate& today, bool removeRecent = false,
                                       RemoveRecent remove = nullptr, void* removeContext = nullptr,
                                       bool deferMove = false);
  CompletionResult recoverCompletionStats(const std::string& cachePath, const std::string& bookPath,
                                          RemoveRecent remove = nullptr, void* removeContext = nullptr);
  // Apply an explicit date override while retaining the current session.
  bool editBookDate(bool started, const ReadingStatsDate& date, BookReadingStats& bookOut,
                    GlobalReadingStats& globalOut, bool& saved);
  // Zero means insufficient tracking data or an unavailable estimate.
  uint32_t bookTimeLeft(int progressBasisPoints) const;
  // Estimate remaining whole pages in the current EPUB section; zero is unavailable.
  uint32_t sectionTimeLeft(int currentPage, int totalPages) const;

 private:
  BookReadingStats book;
  GlobalReadingStats global;
  ReadingStatsSession session;
  StatsSessionLease lease;
  std::string directory;
  uint16_t idleSeconds = 300;
  bool active = false;
  bool completionAvailable = false;
  bool startUnavailable = false;
  bool dirty = false;
#if defined(CROSSPOINT_EXPERIMENTAL_DAILY_STATS)
  bool dailySaveFailed = false;
  bool flushDaily();
#endif
  bool completionRecoveryPending = false;
  bool completionRecoveryTarget = false;
  bool completionRecoveryWasTracking = false;
  ReadingStatsDate completionRecoveryDate;
};

#if defined(CROSSPOINT_EXPERIMENTAL_DAILY_STATS)
static_assert(sizeof(ReaderStats) <= 896, "Review reader statistics RAM growth");
#else
static_assert(sizeof(ReaderStats) <= 640, "Review reader statistics RAM growth");
#endif
