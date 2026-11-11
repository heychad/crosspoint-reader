#include <I18n.h>
#include <Memory.h>

#include "CrossPointSettings.h"
#include "ReaderActivity.h"
#include "activities/util/ConfirmationActivity.h"

#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
void ReaderActivity::confirmStatisticsReset(StatisticsResetPrompt mode) {
  if (!pageRendered.load(std::memory_order_acquire)) return;
  const auto heading =
      mode == StatisticsResetPrompt::Confirm ? StrId::STR_BOOK_STATS_RESET : StrId::STR_STATS_RESET_FAILED;
  const auto body =
      mode == StatisticsResetPrompt::Recovery ? StrId::STR_BOOK_STATS_RESET_RECOVERY : StrId::STR_BOOK_STATS_RESET_BODY;
  const auto accept = mode == StatisticsResetPrompt::Recovery ? StrId::STR_GO_HOME_BUTTON
                      : mode == StatisticsResetPrompt::Retry  ? StrId::STR_RETRY
                                                              : StrId::STR_DELETE;
  auto prompt = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, I18N.get(heading), I18N.get(body),
                                                        tr(STR_CANCEL), I18N.get(accept));
  if (!prompt) {
    LOG_ERR("READER", "OOM: statistics reset confirmation");
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(prompt), [this, mode](const ActivityResult& result) {
    if (result.isCancelled) {
      if (mode == StatisticsResetPrompt::Recovery)
        requestUpdate();
      else
        openReaderMenu();
      return;
    }
    if (mode == StatisticsResetPrompt::Recovery) {
      onGoHome();
      return;
    }
    using Result = statsbackup::BookRestoreResult;
    Result reset = Result::Unavailable;
    {
      RenderLock lock;
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
      constexpr bool completionEnabled = true;
#else
      constexpr bool completionEnabled = false;
#endif
      if (pageRendered.load(std::memory_order_acquire))
        reset = readingStats.resetBookStatistics(getBookCachePath(), bookPath, SETTINGS.trackReadingStats != 0,
                                                 static_cast<uint16_t>(SETTINGS.readingIdleTimeThresholdUnits) * 10U,
                                                 completionEnabled);
      if (readingStats.unavailableAtStart()) pendingStatsLoadWarning.store(true);
    }
    switch (reset) {
      case Result::Restored:
        requestUpdate();
        break;
      case Result::RecoveryRequired:
      case Result::WriteUncertain:
        confirmStatisticsReset(StatisticsResetPrompt::Recovery);
        break;
      case Result::Unavailable:
      case Result::Busy:
      case Result::BackupFailed:
        confirmStatisticsReset(StatisticsResetPrompt::Retry);
        break;
    }
  });
}
#endif
