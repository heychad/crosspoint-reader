#include "EpubReaderActivity.h"

#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION) && defined(CROSSPOINT_EXPERIMENTAL_COMPLETION_PROMPT)
#include <Epub/ReferencePageMap.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Txt.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "activities/util/ConfirmationActivity.h"
#include "util/ReadFolder.h"

void EpubReaderActivity::initializeCompletionPrompt() {
  completionPrompt.configure(-1, 1.0f);
  if (!epub) return;
  constexpr char manifest[] = "META-INF/x-locations.json";
  size_t manifestSize = 0;
  if (!Txt::isTxtOrMd(epub->getPath()) && epub->getItemSize(manifest, &manifestSize) && manifestSize > 0 &&
      manifestSize <= ReferencePageMap::MAX_MANIFEST_BYTES) {
    ReferencePageMap map;
    auto parser = makeUniqueNoThrow<ReferencePageParser>(map, epub->getSpineItemsCount());
    if (parser && parser->ready() && epub->readItemContentsToStream(manifest, *parser, 512) && parser->finish()) {
      if (const auto position = map.targetPercent(99)) {
        completionPrompt.configure(position->spineIndex, position->fraction);
        return;
      }
    }
  }
  const size_t total = epub->getBookSize();
  if (!total) return;
  // Fall back to the existing byte-weighted EPUB progress.
  const size_t target = total - total / 100;
  const int spine = epub->getSpineIndexForSize(target);
  if (spine < 0) return;
  const size_t before = spine > 0 ? epub->getCumulativeSpineItemSize(spine - 1) : 0;
  const size_t through = epub->getCumulativeSpineItemSize(spine);
  if (through <= before || target < before || target > through) return;
  const auto fraction = static_cast<float>(static_cast<double>(target - before) / (through - before));
  completionPrompt.configure(spine, std::clamp(fraction, 0.0f, 1.0f));
}

void EpubReaderActivity::observeCompletionPosition() {
  if (footnoteDepth || !section) return;
  const int pages = section->estimatedTotalPages();
  bool completed = false;
  if (pages <= 0 || section->currentPage < 0 || !readingStats.completionStatus(completed)) return;
  completionPrompt.observe(currentSpineIndex, static_cast<float>(section->currentPage) / pages, completed);
}

bool EpubReaderActivity::showQueuedCompletionPrompt() {
  std::unique_ptr<ConfirmationActivity> prompt;
  {
    RenderLock lock;
    if (!completionPrompt.queued() || inputOverPage()) return false;
    bool completed = false;
    if (!readingStats.completionStatus(completed) || completed
#if !defined(CROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION)
        || (SETTINGS.moveFinishedToReadFolder && !isInReadFolder(bookPath))
#endif
    ) {
      completionPrompt.shown();
      return false;
    }
    prompt = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_COMPLETION_PROMPT_TITLE),
                                                     tr(STR_COMPLETION_PROMPT_BODY));
    if (!prompt) {
      LOG_ERR("ERS", "Could not allocate completion prompt");
      completionPrompt.retryOnNextExit();
      return false;
    }
    readingStats.pause();
    completionPrompt.shown();
    pendingManualTurn = 0;
    pendingManualTurnTouch = false;
  }
  startActivityForResult(std::move(prompt), [this](const ActivityResult& result) {
    {
      RenderLock lock;
      lastPageTurnTime = millis();
    }
    if (result.isCancelled)
      requestUpdate();
    else
      handleCompletionAttempt(false);
  });
  return true;
}

void EpubReaderActivity::handleCompletionAttempt(bool recovery, bool completed) {
  const auto result = recovery ? recoverBookCompletion() : setBookCompleted(completed);
  if (result == ReaderStats::CompletionResult::Applied || result == ReaderStats::CompletionResult::Unchanged) {
    requestUpdate();
    return;
  }
  auto retry = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_COMPLETION_RETRY_TITLE),
                                                       tr(STR_COMPLETION_RETRY_BODY), tr(STR_CANCEL), tr(STR_RETRY));
  if (!retry) {
    LOG_ERR("ERS", "Could not allocate completion retry");
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
