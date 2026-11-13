#include "XtcReaderActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <cstring>

#include "BookReadingStats.h"
#include "CrossPointSettings.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "ProgressFile.h"
#include "ReaderActivity.h"
#include "ReaderUtils.h"
#include "XtcReaderChapterSelectionActivity.h"
#include "XtcReaderMenuActivity.h"
#include "XtcReaderNavigation.h"
#include "activities/settings/SettingsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "personalization/ReaderBarRenderer.h"
#include "util/ReadFolder.h"

bool XtcReaderActivity::loadBook() {
  auto loadedXtc = makeUniqueNoThrow<Xtc>(bookPath, "/.crosspoint");
  if (!loadedXtc) {
    LOG_ERR("XTR", "Failed to allocate XTC object");
    return false;
  }
  if (!loadedXtc->load()) {
    LOG_ERR("XTR", "Failed to load XTC");
    return false;
  }
  xtc = std::move(loadedXtc);
  xtc->setupCacheDir();
  loadProgress();
  return true;
}

void XtcReaderActivity::openChapterSelection(bool returnToMenu) {
  if (xtc && xtc->hasChapters() && !xtc->getChapters().empty()) {
    auto picker = makeUniqueNoThrow<XtcReaderChapterSelectionActivity>(renderer, mappedInput, xtc, currentPage);
    if (!picker) {
      LOG_ERR("XTR", "OOM: chapter picker");
      if (returnToMenu) openReaderMenu();
      return;
    }
    startActivityForResult(std::move(picker), [this, returnToMenu](const ActivityResult& result) {
      if (!result.isCancelled) {
        currentPage = std::get<PageResult>(result.data).page;
        requestUpdate();
      } else if (returnToMenu)
        openReaderMenu();
    });
  }
}

void XtcReaderActivity::openPercentSelection() {
  auto picker = makeUniqueNoThrow<EpubReaderPercentSelectionActivity>(renderer, mappedInput, getProgressPercent());
  if (!picker) {
    LOG_ERR("XTR", "OOM: percent picker");
    openReaderMenu();
    return;
  }
  startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
    const auto* percent = std::get_if<PercentResult>(&result.data);
    if (!result.isCancelled && percent && xtc) {
      RenderLock lock;
      currentPage = xtcPageForPercent(percent->percent, xtc->getPageCount());
      requestUpdate();
    } else
      openReaderMenu();
  });
}

void XtcReaderActivity::openReaderMenu() {
  if (!xtc) return;
  bool completed = false;
  bool completionKnown = false;
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
  completionKnown = completionActionAvailable(completed);
#endif
  auto menu = makeUniqueNoThrow<XtcReaderMenuActivity>(renderer, mappedInput, xtc->getTitle(),
                                                       xtc->hasChapters() && !xtc->getChapters().empty(),
                                                       mappedInput.hasTouch(), completionKnown, completed);
  if (!menu) {
    LOG_ERR("XTR", "OOM: reader menu");
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(menu), [this](const ActivityResult& result) {
    const auto* choice = std::get_if<MenuResult>(&result.data);
    if (result.isCancelled || !choice) return;
    using M = XtcReaderMenuActivity::MenuAction;
    switch (static_cast<M>(choice->action)) {
      case M::Chapters:
        openChapterSelection(true);
        break;
      case M::Percent:
        openPercentSelection();
        break;
      case M::Statistics:
        handleReaderShortcut(readershortcuts::Action::ReadingStatistics);
        break;
      case M::Tracking:
        openReadingStatistics(true);
        break;
      case M::Completion:
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
        toggleCompletion();
#endif
        break;
      case M::QuickActions:
        openQuickActions();
        break;
      case M::Controls: {
        auto settings = makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput, SettingsSection::Controls);
        if (!settings) {
          LOG_ERR("XTR", "OOM: controls");
          openReaderMenu();
          break;
        }
        startActivityForResult(std::move(settings), [this](const ActivityResult&) { openReaderMenu(); });
        break;
      }
      case M::Library:
        handleReaderShortcut(readershortcuts::Action::Library);
        break;
      case M::Home:
        onGoHome();
        break;
      case M::ClearCache:
        confirmCacheReset();
        break;
      case M::ResetStatistics:
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
        confirmStatisticsReset();
#endif
        break;
      case M::Touch:
        break;
    }
  });
}

void XtcReaderActivity::confirmCacheReset(bool retry) {
  if (!xtc) return;
  auto prompt = makeUniqueNoThrow<ConfirmationActivity>(
      renderer, mappedInput, retry ? tr(STR_CLEAR_CACHE_FAILED) : tr(STR_DELETE_CACHE), tr(STR_XTC_CLEAR_IMAGES_BODY),
      tr(STR_CANCEL), retry ? tr(STR_RETRY) : tr(STR_DELETE));
  if (!prompt) {
    LOG_ERR("XTR", "OOM: cache reset confirmation");
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(prompt), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      openReaderMenu();
      return;
    }
    bool cleared = false;
    {
      RenderLock lock;
      cleared = xtc && xtc->clearImageCache();
    }
    if (cleared)
      requestUpdate();
    else
      confirmCacheReset(true);
  });
}

#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
bool XtcReaderActivity::completionActionAvailable(bool& completed) const {
  RenderLock lock;
  return pageRendered.load(std::memory_order_acquire) && readingStats.completionStatus(completed) &&
         (completed || !SETTINGS.moveFinishedToReadFolder || isInReadFolder(bookPath));
}

void XtcReaderActivity::toggleCompletion() {
  bool completed = false;
  if (completionActionAvailable(completed)) handleCompletionAttempt(false, !completed);
}
#endif

bool XtcReaderActivity::supportsReaderShortcut(readershortcuts::Action action) const {
  if (action == readershortcuts::Action::Menu) return true;
  if (action == readershortcuts::Action::ToggleCompletion) {
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
    bool completed = false;
    return completionActionAvailable(completed);
#else
    return false;
#endif
  }
  if (action == readershortcuts::Action::SelectChapter) return xtc && xtc->hasChapters() && !xtc->getChapters().empty();
  return ReaderActivity::supportsReaderShortcut(action);
}

bool XtcReaderActivity::handleReaderShortcut(readershortcuts::Action action, QuickLockTrigger trigger) {
  if (action == readershortcuts::Action::ToggleCompletion) {
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
    toggleCompletion();
#endif
    return true;
  }
  if (action == readershortcuts::Action::Menu) {
    openReaderMenu();
    return true;
  }
  if (action == readershortcuts::Action::SelectChapter) {
    openChapterSelection();
    return true;
  }
  return ReaderActivity::handleReaderShortcut(action, trigger);
}

bool XtcReaderActivity::handleFormatInput() {
  if (!xtc) {
    return false;
  }

  const auto mode = SETTINGS.statusBarSpec().xtcMode;
  int topHeight = 0, bottomHeight = 0;
  if (SETTINGS.readerBarsEnabled) {
    const auto top = xtcReaderBarForDisplay(ReaderBarPosition::Top, SETTINGS.legacyXtcTopUsesBottom,
                                            SETTINGS.readerTopBar, SETTINGS.readerBottomBar);
    topHeight = readerbars::height(renderer, top);
    bottomHeight = readerbars::height(renderer, SETTINGS.readerBottomBar);
  } else {
    topHeight = bottomHeight = UITheme::getInstance().getStatusBarHeight();
  }
  if (handleStatusBarTap(
          mode == CrossPointSettings::XTC_STATUS_BAR_TOP || mode == CrossPointSettings::XTC_STATUS_BAR_BOTH ? topHeight
                                                                                                            : 0,
          mode == CrossPointSettings::XTC_STATUS_BAR_BOTTOM || mode == CrossPointSettings::XTC_STATUS_BAR_BOTH
              ? bottomHeight
              : 0,
          SETTINGS.readerBarsEnabled ? -5 : 0))
    return true;

  // Open the menu on Confirm release or the reader's touch menu gesture.
  const bool touchMenu = ReaderUtils::isTouchMenuGesture(renderer, mappedInput);
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || touchMenu) {
    if (touchMenu) haptic_feedback::touchAction();
    openReaderMenu();
    return true;
  }
  return false;
}

void XtcReaderActivity::applyInitialOrientation() { renderer.setOrientation(GfxRenderer::Orientation::Portrait); }

void XtcReaderActivity::renderBook() {
  if (!xtc) {
    return;
  }

  renderPage();
  saveProgress();
}

XtcReaderActivity::StatusBarInfo XtcReaderActivity::getStatusBarInfo(bool chapterTitle, bool bookTitle) const {
  const int bookPageCount = static_cast<int>(xtc->getPageCount());
  const int bookPage = static_cast<int>(currentPage) + 1;
  std::string title = bookTitle ? xtc->getTitle() : "";

  if (!xtc->hasChapters()) {
    return StatusBarInfo{bookPage, bookPageCount, std::move(title)};
  }

  const auto& chapters = xtc->getChapters();
  const auto chapterIt = std::find_if(chapters.begin(), chapters.end(), [this](const xtc::ChapterInfo& chapter) {
    return currentPage >= chapter.startPage && currentPage <= chapter.endPage;
  });

  if (chapterIt == chapters.end() || chapterIt->endPage < chapterIt->startPage) {
    return StatusBarInfo{bookPage, bookPageCount, std::move(title)};
  }

  if (chapterTitle) {
    title = chapterIt->name.empty() ? tr(STR_UNNAMED) : chapterIt->name;
  }

  return StatusBarInfo{static_cast<int>(currentPage - chapterIt->startPage) + 1,
                       static_cast<int>(chapterIt->endPage - chapterIt->startPage) + 1, std::move(title)};
}

void XtcReaderActivity::renderStatusBarOverlay(GfxRenderer& renderer, const StatusBarOverlayPosition position,
                                               const bool drawContent) const {
  const auto sb = SETTINGS.statusBarSpec();
  const bool top = position == StatusBarOverlayPosition::Top;
  const bool enabled =
      sb.xtcMode == CrossPointSettings::XTC_STATUS_BAR_BOTH ||
      sb.xtcMode == (top ? CrossPointSettings::XTC_STATUS_BAR_TOP : CrossPointSettings::XTC_STATUS_BAR_BOTTOM);
  if (!enabled) return;
  auto config =
      xtcReaderBarForDisplay(top ? ReaderBarPosition::Top : ReaderBarPosition::Bottom, SETTINGS.legacyXtcTopUsesBottom,
                             SETTINGS.readerTopBar, SETTINGS.readerBottomBar);
  const bool hidden = SETTINGS.readerBarsEnabled && config.hidden;
  config.hidden = false;
  const int height =
      SETTINGS.readerBarsEnabled ? readerbars::height(renderer, config) : UITheme::getInstance().getStatusBarHeight();
  if (height <= 0) return;
  int mt, mr, mb, ml;
  renderer.getOrientedViewableTRBL(&mt, &mr, &mb, &ml);
  const int screenHeight = renderer.getScreenHeight();
  const int origin =
      top ? (SETTINGS.readerBarsEnabled ? std::max(0, mt - 5) : mt) : std::max(0, screenHeight - mb - height - 4);
  const int clearHeight = std::min(std::max(0, screenHeight - origin), top ? height + 4 : screenHeight - mb - origin);
  if (clearHeight > 0) renderer.fillRect(0, origin, renderer.getScreenWidth(), clearHeight, false);
  // XTC bitmaps can contain their own strip. Clear it even when firmware bars are hidden,
  // and keep grayscale mask passes free of bitmap-strip ink.
  if (hidden || (SETTINGS.tapToHideStatusBar && !statusBarsVisible.load()) || !drawContent) return;
  const int pages = static_cast<int>(xtc->getPageCount());
  const float progress = pages > 0 ? (static_cast<float>(currentPage + 1) * 100.0f) / pages : 0;
  if (SETTINGS.readerBarsEnabled) {
    const auto info = getStatusBarInfo(config.contains(ReaderBarItem::ChapterTitle), false);
    const auto title = config.contains(ReaderBarItem::BookTitle) ? xtc->getTitle() : std::string{};
    char bookTime[32] = "-", chapterTime[32] = "-";
    const auto formatTime = [](uint32_t seconds, char* out, size_t length) {
      if (!seconds) return;
      out[0] = '~';
      BookReadingStats::formatDuration(seconds, out + 1, length - 1);
      out[utf8SafeTruncateBuffer(out, static_cast<int>(strlen(out)))] = '\0';
    };
    if (config.contains(ReaderBarItem::TimeLeftBook))
      formatTime(readingStats.bookTimeLeft(getProgressBasisPoints()), bookTime, sizeof(bookTime));
    bool chapterEstimate = config.contains(ReaderBarItem::TimeLeftSection);
#if defined(CROSSPOINT_EXPERIMENTAL_CHAPTER_TIME)
    chapterEstimate |= config.contains(ReaderBarItem::TimeLeftChapter);
#endif
    if (chapterEstimate)
      formatTime(readingStats.sectionTimeLeft(info.currentPage, info.pageCount), chapterTime, sizeof(chapterTime));
    readerbars::draw(renderer, config, top ? ReaderBarPosition::Top : ReaderBarPosition::Bottom, progress,
                     info.currentPage, info.pageCount, false, title.c_str(), info.title.c_str(), false, nullptr,
                     bookTime, chapterTime, chapterTime);
  } else {
    const auto info = getStatusBarInfo(sb.titleMode == CrossPointSettings::CHAPTER_TITLE,
                                       sb.titleMode == CrossPointSettings::BOOK_TITLE);
    const int paddingBottom = top ? screenHeight - height - mb - mt - 4 : 0;
    GUI.drawStatusBar(renderer, progress, info.currentPage, info.pageCount, info.title, paddingBottom);
  }
}

bool XtcReaderActivity::renderPage(PageRenderTarget target) {
  if (!xtc || currentPage >= xtc->getPageCount()) return false;
  const bool bufferOnly = target == PageRenderTarget::Buffer;
  const uint16_t pageWidth = xtc->getPageWidth();
  const uint16_t pageHeight = xtc->getPageHeight();
  const uint8_t bitDepth = xtc->getBitDepth();
  if (pageWidth == 0 || pageHeight == 0 || (bitDepth != 1 && bitDepth != 2)) return false;

  size_t pageBufferSize;
  if (bitDepth == 2) {
    pageBufferSize = static_cast<size_t>(pageWidth) * ((static_cast<size_t>(pageHeight) + 7) / 8) * 2;
  } else {
    pageBufferSize = ((pageWidth + 7) / 8) * pageHeight;
  }

  auto pageBuffer = makeUniqueNoThrow<uint8_t[]>(pageBufferSize);
  if (!pageBuffer) {
    LOG_ERR("XTR", "Failed to allocate page buffer (%lu bytes)", pageBufferSize);
    if (bufferOnly) return false;
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_MEMORY_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return false;
  }

  size_t bytesRead = xtc->loadPage(currentPage, pageBuffer.get(), pageBufferSize);
  if (bytesRead != pageBufferSize) {
    LOG_ERR("XTR", "Failed to load page %lu: bufferSize=%lu bitDepth=%u error=%s", currentPage, pageBufferSize,
            bitDepth, xtc::errorToString(xtc->getLastError()));
    if (bufferOnly) return false;
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, 300, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return false;
  }

  renderer.clearScreen();

  const auto overlays = [this](bool content = true) {
    renderStatusBarOverlay(renderer, StatusBarOverlayPosition::Top, content);
    renderStatusBarOverlay(renderer, StatusBarOverlayPosition::Bottom, content);
  };
  const uint16_t maxSrcY = pageHeight;

  if (bitDepth == 2) {
    const size_t planeSize = static_cast<size_t>(pageWidth) * ((static_cast<size_t>(pageHeight) + 7) / 8);
    const uint8_t* plane1 = pageBuffer.get();
    const uint8_t* plane2 = pageBuffer.get() + planeSize;
    const size_t colBytes = (pageHeight + 7) / 8;

    auto getPixelValue = [&](uint16_t x, uint16_t y) -> uint8_t {
      const size_t colIndex = pageWidth - 1 - x;
      const size_t byteInCol = y / 8;
      const size_t bitInByte = 7 - (y % 8);
      const size_t byteOffset = colIndex * colBytes + byteInCol;
      const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
      const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
      return (bit1 << 1) | bit2;
    };

    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        if (getPixelValue(x, y) >= 1) {
          renderer.drawPixel(x, y, true);
        }
      }
    }

    overlays();
    if (bufferOnly) return true;

    if (pagesUntilFullRefresh <= 1) {
      // Periodic ghost cleanup: scrub via the normal path, then run the
      // settle flavor of the grayscale base pass (DTM planes are equal after
      // the display sync, so only the gentle reinforcement cells fire).
      // Combined-base panels (Paper Mono) instead defer the base so the gray
      // planes below join it in one waveform.
      if (renderer.grayscaleCapabilities().base == HalDisplay::GrayscaleBase::Combined) {
        renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
      } else {
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        renderer.preconditionGrayscale();
      }
      pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    } else {
      renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
      pagesUntilFullRefresh--;
    }

    renderer.clearScreen(0x00);
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        if (getPixelValue(x, y) == 1) {
          renderer.drawPixel(x, y, false);
        }
      }
    }
    overlays(false);
    renderer.copyGrayscaleLsbBuffers();

    renderer.clearScreen(0x00);
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        const uint8_t pv = getPixelValue(x, y);
        if (pv == 1 || pv == 2) {
          renderer.drawPixel(x, y, false);
        }
      }
    }
    overlays(false);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();

    renderer.clearScreen();
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        if (getPixelValue(x, y) >= 1) {
          renderer.drawPixel(x, y, true);
        }
      }
    }

    overlays();
    renderer.cleanupGrayscaleWithFrameBuffer();

    pageBuffer.reset();
    LOG_DBG("XTR", "Rendered page %lu/%lu (2-bit grayscale)", currentPage + 1, xtc->getPageCount());
    markPageRendered();
    return true;
  } else {
    const size_t srcRowBytes = (pageWidth + 7) / 8;

    for (uint16_t srcY = 0; srcY < maxSrcY; srcY++) {
      const size_t srcRowStart = srcY * srcRowBytes;

      for (uint16_t srcX = 0; srcX < pageWidth; srcX++) {
        const size_t srcByte = srcRowStart + srcX / 8;
        const size_t srcBit = 7 - (srcX % 8);
        const bool isBlack = !((pageBuffer[srcByte] >> srcBit) & 1);

        if (isBlack) {
          renderer.drawPixel(srcX, srcY, true);
        }
      }
    }
  }

  pageBuffer.reset();
  overlays();

  if (bufferOnly) return true;
  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);

  LOG_DBG("XTR", "Rendered page %lu/%lu (%u-bit)", currentPage + 1, xtc->getPageCount(), bitDepth);
  markPageRendered();
  return true;
}

#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
bool XtcReaderActivity::restorePageForSleep(SleepPageTarget target) {
  if (!renderer.hasFrameBuffer()) return false;
  renderer.setOrientation(GfxRenderer::Portrait);
  return renderPage(target == SleepPageTarget::Display ? PageRenderTarget::Display : PageRenderTarget::Buffer);
}
#endif

bool XtcReaderActivity::pageTurn(bool isForward) {
  if (!xtc) return false;
  if (isForward) {
    if (currentPage < xtc->getPageCount()) {
      currentPage++;
      return true;
    }
  } else {
    if (currentPage > 0) {
      currentPage--;
      return true;
    }
  }
  return false;
}

bool XtcReaderActivity::skipPages(int amount) {
  if (!xtc) return false;
  int newPage = static_cast<int>(currentPage) + amount;
  if (newPage < 0) newPage = 0;
  if (newPage > static_cast<int>(xtc->getPageCount())) newPage = static_cast<int>(xtc->getPageCount());
  if (newPage != static_cast<int>(currentPage)) {
    currentPage = static_cast<uint32_t>(newPage);
    return true;
  }
  return false;
}

bool XtcReaderActivity::isAtEndOfBook() const { return xtc && currentPage >= xtc->getPageCount(); }

void XtcReaderActivity::onReturnFromEndOfBook() {
  if (xtc && xtc->getPageCount() > 0) {
    currentPage = xtc->getPageCount() - 1;
  } else {
    currentPage = 0;
  }
}

void XtcReaderActivity::saveProgress() const {
  if (!xtc) return;
  uint8_t data[4];
  data[0] = currentPage & 0xFF;
  data[1] = (currentPage >> 8) & 0xFF;
  data[2] = (currentPage >> 16) & 0xFF;
  data[3] = (currentPage >> 24) & 0xFF;
  if (!ProgressFile::writeAtomic(xtc->getCachePath(), data, sizeof(data))) {
    LOG_ERR("XTC", "Failed to save progress: page %lu", currentPage);
  }
}

void XtcReaderActivity::loadProgress() {
  if (!xtc) return;
  HalFile f;
  if (Storage.openFileForRead("XTC", xtc->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
      if (currentPage >= xtc->getPageCount() && xtc->getPageCount() > 0) {
        currentPage = xtc->getPageCount() - 1;
      }
      LOG_DBG("XTC", "Loaded progress: page %lu/%lu", currentPage + 1, xtc->getPageCount());
    }
  }
}

ScreenshotInfo XtcReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Xtc;
  if (xtc) {
    const std::string t = xtc->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
    const uint32_t pageCount = xtc->getPageCount();
    info.totalPages = pageCount;
    uint32_t clampedPage = (pageCount > 0 && currentPage >= pageCount) ? pageCount - 1 : currentPage;
    info.progressPercent = pageCount > 0 ? xtc->calculateProgress(clampedPage) : 0;
    // XTC progress already counts the visible page as read.
    info.endProgressPercent = info.progressPercent;
    info.currentPage = static_cast<int>(clampedPage) + 1;
  } else {
    info.currentPage = currentPage + 1;
  }
  return info;
}
