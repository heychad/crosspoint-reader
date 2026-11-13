#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>
#include <Memory.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "HapticFeedback.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "activities/RenderLock.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/reader/ReadingStatsStore.h"
#include "activities/reader/StatsSaveNotice.h"
#include "activities/settings/ReadingStatsActivity.h"
#if defined(CROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION) && \
    (!defined(CROSSPOINT_EXPERIMENTAL_COMPLETION) || !defined(CROSSPOINT_EXPERIMENTAL_COMPLETION_MOVE))
#error "Deferred automation requires completion and completion-move support"
#endif
#if defined(CROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION)
#include "activities/reader/DeferredCompletionMove.h"
#include "activities/reader/ReadingStatsUtils.h"
#endif
#include "components/UITheme.h"
#include "components/themes/minimal/CompactHomeLayout.h"
#include "components/themes/minimal/DashboardHomeTheme.h"
#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)
#include "components/themes/lyra/CarouselLayout.h"
#include "components/themes/lyra/LyraCarouselTheme.h"
#endif
#include "fontIds.h"
#include "util/BookProgress.h"

namespace {
bool usesCompactHome() {
  return SETTINGS.uiTheme == CrossPointSettings::MINIMAL_HOME || SETTINGS.uiTheme == CrossPointSettings::DASHBOARD_HOME;
}
}  // namespace

void HomeActivity::activateSelection() {
  if (selectorIndex < recentBooks.size()) {
    onRecentBookSelected(recentBooks[selectorIndex].path);
    return;
  }
  const int menuIndex = selectorIndex - static_cast<int>(recentBooks.size());
  switch (indexToMenuItem(menuIndex, hasLibrarySlot(), libraryFirstOnHome)) {
    case HomeMenuItem::FILE_BROWSER:
      onFileBrowserOpen();
      break;
    case HomeMenuItem::LIBRARY:
      onLibraryOpen();
      break;
    case HomeMenuItem::SAVED_ITEMS:
      activityManager.goToSavedItems();
      break;
    case HomeMenuItem::OPDS_BROWSER:  // the library slot
      hasPlugins ? onPluginsOpen() : onOpdsBrowserOpen();
      break;
    case HomeMenuItem::FILE_TRANSFER:
      onFileTransferOpen();
      break;
    case HomeMenuItem::SETTINGS_MENU:
      onSettingsOpen();
      break;
    default:
      break;
  }
}

void HomeActivity::showCompactMenu() {
  const char* options[7] = {tr(STR_BROWSE_FILES), tr(STR_LIBRARY), tr(STR_SAVED_ITEMS), tr(STR_FILE_TRANSFER),
                            tr(STR_SETTINGS_TITLE)};
  if (libraryFirstOnHome) std::swap(options[0], options[1]);
  const int menuCount = getMenuItemCount() - static_cast<int>(recentBooks.size());
  if (hasLibrarySlot()) {
    options[5] = options[4];
    options[4] = options[3];
    options[3] = hasPlugins ? tr(STR_PLUGINS) : tr(STR_OPDS_BROWSER);
  }
  int count = menuCount;
  if (SETTINGS.trackReadingStats) options[count++] = tr(STR_READING_STATISTICS);
  popup.show(tr(STR_MENU), options, count, 0, [this, menuCount](int index) {
    if (index == menuCount) {
      openCompactStatistics();
      return;
    }
    selectorIndex = static_cast<int>(recentBooks.size()) + index;
    activateSelection();
  });
  requestUpdate();
}

void HomeActivity::openCompactStatistics() {
  auto screen = recentBooks.empty() ? makeUniqueNoThrow<ReadingStatsActivity>(renderer, mappedInput)
                                    : ReadingStatsActivity::forClosedBook(
                                          renderer, mappedInput, recentBooks.front().path, recentBooks.front().title);
  if (!screen) {
    LOG_ERR("HOME", "Cannot open reading statistics");
    statisticsOpenFailed = true;
    requestUpdate();
    return;
  }
  {
    RenderLock lock;
    freeCoverBuffer();
    coverRendered = false;
  }
  startActivityForResult(std::move(screen), [this](const ActivityResult&) {
    RenderLock lock;
    const auto& stored = RECENT_BOOKS.getBooks();
    if (!recentBooks.empty() && (RecentBooksStore::isMissing(recentBooks.front()) ||
                                 std::find(stored.begin(), stored.end(), recentBooks.front()) == stored.end())) {
      loadRecentBooks(1);
      hasContinueReading = !recentBooks.empty();
    }
    selectorIndex = 0;
    recentsLoaded = false;
    if (usesCompactHome()) loadDashboardData();
    requestUpdate();
  });
}

void HomeActivity::showNextCompactBook() {
  if (recentBooks.empty()) return;
  const auto& books = RECENT_BOOKS.getBooks();
  const auto current = std::find(books.begin(), books.end(), recentBooks.front());
  const size_t start = current == books.end() ? 0 : static_cast<size_t>(current - books.begin()) + 1;
  for (size_t step = 0; step < books.size(); ++step) {
    const auto& next = books[(start + step) % books.size()];
    if (next.path == recentBooks.front().path || RecentBooksStore::isMissing(next)) continue;
    {
      RenderLock lock;
      recentBooks.front() = next;
      selectorIndex = 0;
      freeCoverBuffer();
      coverRendered = false;
      recentsLoaded = false;
      if (usesCompactHome()) loadDashboardData();
    }
    requestUpdate();
    return;
  }
}

bool HomeActivity::handleCompactHome() {
  if (!usesCompactHome()) return false;
  switch (mappedInput.wasSwipe()) {
    case MappedInputManager::SwipeDir::Up:
      showCompactMenu();
      return true;
    case MappedInputManager::SwipeDir::Right:
      libraryFirstOnHome ? onLibraryOpen() : onFileBrowserOpen();
      return true;
    case MappedInputManager::SwipeDir::Down:
      onSettingsOpen();
      return true;
    case MappedInputManager::SwipeDir::Left:
      showNextCompactBook();
      return true;
    case MappedInputManager::SwipeDir::None:
      break;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    showCompactMenu();
    return true;
  }
  const Rect content = UITheme::getInstance().getContentArea(renderer);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect area{content.x, metrics.homeTopPadding, content.width, homeCoverTileHeight()};
  const Rect cover = GUI.hasDashboardHome() ? DashboardHomeTheme::coverRect(renderer, area)
                                            : MinimalHomeTheme::coverRect(renderer, area);
  int touched = -1;
  const auto touch = mappedInput.colTouch(touched, cover.x, cover.width, recentBooks.empty() ? 0 : 1, cover.y,
                                          cover.y + cover.height, cover.width);
  if (touch == MappedInputManager::RowTouch::Tap || mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!recentBooks.empty()) {
      haptic_feedback::touchAction();
      onRecentBookSelected(recentBooks.front().path);
    }
  }
  return true;
}

int HomeActivity::getMenuItemCount() const {
  int count = 5;  // File Browser, Library, Saved Items, File transfer, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasLibrarySlot()) {
    count++;
  }
  return count;
}

int HomeActivity::homeCoverTileHeight() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (usesCompactHome()) return compacthomelayout::tileHeight(renderer.getScreenHeight(), metrics.homeTopPadding);
#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)
  if (SETTINGS.uiTheme == CrossPointSettings::LYRA_CAROUSEL)
    return carousellayout::coverHeight(metrics.homeCoverTileHeight, renderer.getScreenHeight(), metrics.homeTopPadding,
                                       metrics.buttonHintsHeight, renderer.getLineHeight(SMALL_FONT_ID));
#endif
  return metrics.homeCoverTileHeight;
}

HomeActivity::MenuPage HomeActivity::menuPage() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int rowHeight = GUI.getMenuRowHeight(renderer);
  const int footerTop = renderer.getScreenHeight() - metrics.buttonHintsHeight;
  const int desiredTop =
      metrics.homeTopPadding + homeCoverTileHeight() + metrics.homeMenuTopOffset + GUI.getMenuTopInset();
  const int top = std::min(desiredTop, std::max(0, footerTop - rowHeight));
  const int capacity = std::max(1, (footerTop - top + metrics.menuSpacing) / (rowHeight + metrics.menuSpacing));
  const int bookRows = metrics.homeContinueReadingInMenu ? 0 : static_cast<int>(recentBooks.size());
  const int total = getMenuItemCount() - bookRows;
#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)
  if (SETTINGS.uiTheme == CrossPointSettings::LYRA_CAROUSEL) {
    return {std::max(0, footerTop - carousellayout::menuHeight(renderer.getLineHeight(SMALL_FONT_ID))), 0, total,
            total};
  }
#endif
  const int selected = std::clamp(selectorIndex - bookRows, 0, total - 1);
  const int first = (selected / capacity) * capacity;
  return {top, first, std::min(capacity, total - first), capacity};
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(coverGridUi ? maxBooks : std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::fillCoverGridFromLibrary() {
  if (recentBooks.size() >= CoverGridHomeUi::MAX_BOOKS) return;
  // Keep the index and record together off the task stack; reuse for every row.
  struct LibraryReader {
    library::LibraryIndexFile index;
    library::ClixRecord record;
  };
  auto reader = makeUniqueNoThrow<LibraryReader>();
  if (!reader) {
    LOG_ERR("HOME", "OOM: library index");
    return;
  }
  auto& index = reader->index;
  auto& record = reader->record;
  if (!index.open(library::libraryIndexPath())) {
    index.close();
    GUI.drawPopup(renderer, tr(STR_LIBRARY_REBUILDING));
    library::BuildStats stats;
    if (!library::buildLibraryIndex("/", stats, SETTINGS.libraryUseMetadata != 0) ||
        !index.open(library::libraryIndexPath())) {
      LOG_ERR("HOME", "Cannot populate cover grid from library");
      return;
    }
  }
  for (uint16_t row = 0; row < index.bookCount() && recentBooks.size() < CoverGridHomeUi::MAX_BOOKS; ++row) {
    RecentBook book;
    if (!index.readRecord(index.ordinalForRow(library::SortOrder::RecentDesc, row), record) ||
        !index.readPath(record, book.path))
      continue;
    if (std::any_of(recentBooks.begin(), recentBooks.end(),
                    [&](const RecentBook& existing) { return existing.path == book.path; }) ||
        RecentBooksStore::isMissing(book))
      continue;
    if (!index.readTitle(record, book.title) && !index.readName(record, book.title)) continue;
    index.readAuthor(record, book.author);
    if (index.ioFailed()) break;
    recentBooks.push_back(std::move(book));
  }
}

void HomeActivity::resolveGridCoverPaths() {
  for (auto& book : recentBooks) {
    if (!book.coverBmpPath.empty()) continue;
    // Constructors only derive cache paths; no metadata parsing or image generation.
    // Keep these large objects off the task stack and release each before the next book.
    if (FsHelpers::hasReflowableBookExtension(book.path)) {
      auto epub = makeUniqueNoThrow<Epub>(book.path, "/.crosspoint");
      if (!epub) {
        LOG_ERR("HOME", "OOM: EPUB thumbnail path");
        continue;
      }
      book.coverBmpPath = epub->getThumbBmpPath();
    } else if (FsHelpers::hasXtcExtension(book.path)) {
      auto xtc = makeUniqueNoThrow<Xtc>(book.path, "/.crosspoint");
      if (!xtc) {
        LOG_ERR("HOME", "OOM: XTC thumbnail path");
        continue;
      }
      book.coverBmpPath = xtc->getThumbBmpPath();
    }
  }
}

void HomeActivity::loadGridCover(RecentBook& book, int height, bool& showingLoading, Rect& popupRect) {
  if (!book.coverBmpPath.empty() && Storage.exists(UITheme::getCoverThumbPath(book.coverBmpPath, height).c_str()))
    return;
  // Only one parser lives at a time; EPUB/XTC objects exceed the stack budget.
  if (FsHelpers::hasReflowableBookExtension(book.path)) {
    auto epub = makeUniqueNoThrow<Epub>(book.path, "/.crosspoint");
    if (!epub) {
      LOG_ERR("HOME", "OOM: cover EPUB");
      return;
    }
    book.coverBmpPath = epub->getThumbBmpPath();
    if (Storage.exists(epub->getThumbBmpPath(height).c_str())) return;
    if (!showingLoading) {
      showingLoading = true;
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      GUI.fillPopupProgress(renderer, popupRect, 0);
    }
    if (epub->generateThumbBmpFromSource(height)) {
      return;
    }
  } else if (FsHelpers::hasXtcExtension(book.path)) {
    auto xtc = makeUniqueNoThrow<Xtc>(book.path, "/.crosspoint");
    if (!xtc) {
      LOG_ERR("HOME", "OOM: cover XTC");
      return;
    }
    book.coverBmpPath = xtc->getThumbBmpPath();
    if (Storage.exists(xtc->getThumbBmpPath(height).c_str())) return;
    if (!showingLoading) {
      showingLoading = true;
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      GUI.fillPopupProgress(renderer, popupRect, 0);
    }
    if (xtc->load() && xtc->generateThumbBmp(height)) {
      return;
    }
  }
  book.coverBmpPath.clear();
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    // The cover grid shares one slot size; generating at any other height
    // would rescale the dithered thumb at draw time and alias badly.
    const int thumbHeight = coverGridUi ? coverGridUi->thumbHeightFor() : coverHeight;
    if (coverGridUi) {
      loadGridCover(book, thumbHeight, showingLoading, popupRect);
      ++progress;
      if (showingLoading) GUI.fillPopupProgress(renderer, popupRect, progress * 100 / recentBooks.size());
      continue;
    }
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, thumbHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub/txt/md, try to load the metadata for title/author and cover
        if (FsHelpers::hasReflowableBookExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(thumbHeight);
          if (!success) {
            // A failed size-specific thumbnail must not erase the shared path.
            book.coverBmpPath.clear();
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(thumbHeight);
            if (!success) {
              book.coverBmpPath.clear();
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::loadDashboardData() {
  dashboardData = {};
  dashboardDetails = {};
  if (recentBooks.empty()) return;
  const auto& path = recentBooks.front().path;
  // TXT/Markdown validation can delete stale caches even in read-only loads.
  if (FsHelpers::hasEpubExtension(path))
    dashboardData.progress = GUI.hasDashboardHome()
                                 ? loadBookProgress(path, dashboardDetails.chapter, sizeof(dashboardDetails.chapter))
                                 : loadBookProgress(path);
  else if (FsHelpers::hasXtcExtension(path))
    dashboardData.progress = loadBookProgress(path);
  if (!SETTINGS.trackReadingStats) return;
  std::string cache;
  if (FsHelpers::hasReflowableBookExtension(path)) {
    cache = Epub::cachePathForFilePath(path, "/.crosspoint");
  } else if (FsHelpers::hasXtcExtension(path)) {
    auto xtc = makeUniqueNoThrow<Xtc>(path, "/.crosspoint");
    if (!xtc) return;
    cache = xtc->getCachePath();
  } else {
    return;
  }
  auto stats = makeUniqueNoThrow<BookReadingStats>();
  if (!stats) return;
  bool enabled = false;
  bool found = false;
  *stats = readingstats::loadBook(cache, path, enabled, &found, BookReadingStats::LoadMode::ReadOnly);
  if (enabled && found && stats->writable) {
    dashboardData.hasStats = true;
    dashboardData.readingSeconds = stats->totalReadingSeconds;
    if (dashboardData.progress >= 0 && !stats->isCompleted)
      dashboardData.remainingSeconds = stats->remainingReadingSeconds(dashboardData.progress * 100);
    dashboardDetails.sessions = stats->sessionCount;
    dashboardDetails.completed = stats->isCompleted;
    dashboardDetails.startDate = stats->startDate;
    if (stats->sessionCount) dashboardDetails.averageSessionSeconds = stats->totalReadingSeconds / stats->sessionCount;
    if (stats->totalReadingSeconds > 60) {
      const uint64_t tenths = static_cast<uint64_t>(stats->totalPagesTurned) * 600 / stats->totalReadingSeconds;
      dashboardDetails.pagesPerMinuteTenths = static_cast<uint32_t>(std::min<uint64_t>(tenths, UINT32_MAX));
      dashboardDetails.hasPace = true;
    }
  }
  if (!GUI.hasDashboardHome()) return;
  ReadingStatsDateTime now{};
  dashboardDetails.hasClock = getCurrentLocalReadingStatsDateTime(now) && now.date.isValid();
  if (dashboardData.hasStats) {
    const auto end = stats->isCompleted && stats->finishedDate.isValid() ? stats->finishedDate
                     : dashboardDetails.hasClock                         ? now.date
                                                                         : ReadingStatsDate{};
    if (stats->startDate.isValid() && end.isValid() && compareReadingStatsDate(end, stats->startDate) >= 0) {
      dashboardDetails.daysReading = readingSpanDaysElapsed(stats->startDate, end);
      dashboardDetails.hasDays = true;
      dashboardDetails.dailyAverageSeconds =
          stats->totalReadingSeconds / std::max<uint16_t>(1, dashboardDetails.daysReading);
    }
    dashboardDetails.finishDate = stats->isCompleted ? stats->finishedDate
                                  : dashboardDetails.hasClock && dashboardData.progress >= 0
                                      ? stats->projectedFinishDate(now, dashboardData.progress * 100)
                                      : ReadingStatsDate{};
  }
  stats.reset();
  if (Storage.lookupPath("/.crosspoint/global_stats.bin") != HalStorage::PathStatus::Present) return;
  auto global = makeUniqueNoThrow<GlobalReadingStats>();
  if (!global) return;
  *global = GlobalReadingStats::loadReadOnly();
  if (!global->writable) return;
  dashboardDetails.hasGlobal = true;
  dashboardDetails.globalSeconds = global->totalReadingSeconds;
  dashboardDetails.completedBooks = global->completedBooks;
  if (dashboardDetails.hasClock) dashboardDetails.currentStreak = global->currentReadingStreak(&now.date);
  uint32_t peak = 0;
  for (size_t i = 0; i < global->timeOfDaySeconds.size(); ++i) {
    if (global->timeOfDaySeconds[i] <= peak) continue;
    peak = global->timeOfDaySeconds[i];
    dashboardDetails.readerType = static_cast<ReadingTimeBucket>(i);
    dashboardDetails.hasReaderType = true;
  }
}

#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)
static HomeDashboardData loadCarouselBookData(const std::string& path) {
  HomeDashboardData data;
  if (FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path)) data.progress = loadBookProgress(path);
  if (!SETTINGS.trackReadingStats) return data;
  std::string cache;
  if (FsHelpers::hasEpubExtension(path)) {
    cache = Epub::cachePathForFilePath(path, "/.crosspoint");
  } else if (FsHelpers::hasXtcExtension(path)) {
    auto xtc = makeUniqueNoThrow<Xtc>(path, "/.crosspoint");
    if (!xtc) return data;
    cache = xtc->getCachePath();
  } else {
    return data;
  }
  auto stats = makeUniqueNoThrow<BookReadingStats>();
  if (!stats) return data;
  bool enabled = false, found = false;
  *stats = readingstats::loadBook(cache, path, enabled, &found, BookReadingStats::LoadMode::ReadOnly);
  if (enabled && found && stats->writable && stats->sessionCount > 0) {
    data.hasStats = true;
    data.readingSeconds = stats->totalReadingSeconds;
  }
  return data;
}
#endif

#if defined(CROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION)
void HomeActivity::showDeferredMove(void* context) {
  auto& home = *static_cast<HomeActivity*>(context);
  RenderLock lock;
  home.renderer.clearScreen();
  GUI.drawPopup(home.renderer, tr(STR_ORGANIZING_FINISHED_BOOK));
  home.deferredMoveOverlayShown = true;
}
#endif

void HomeActivity::onEnter() {
  Activity::onEnter();
#if defined(CROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION)
  ReadingStatsDateTime now{};
  const ReadingStatsDate today = getCurrentLocalReadingStatsDateTime(now) ? now.date : ReadingStatsDate{};
  const auto pending = readingstats::deferredcompletion::executeOnePending(today, showDeferredMove, this);
  using MoveResult = readingstats::deferredcompletion::ExecuteMoveResult;
  deferredMoveRetryPending = pending == MoveResult::Moved || pending == MoveResult::Cancelled ||
                             pending == MoveResult::Busy || pending == MoveResult::Unavailable;
  if (deferredMoveRetryPending) deferredMoveRetryAt = millis() + 500;
  deferredMoveNeedsReview = pending == MoveResult::Blocked || pending == MoveResult::RecoveryRequired;
  deferredMoveOverlayShown = false;
#endif
#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)
  carouselCenterIndex = 0;
#endif

  libraryFirstOnHome = SETTINGS.libraryFirstOnHome != 0;
  hasOpdsServers = OPDS_STORE.hasServers();
  // Plugin System is the global opt-in. Once enabled, expose the Plugins entry
  // even when no SD plugins are installed yet so Plugin Hub can be installed
  // from inside the Plugins screen (or users can sideload plugins manually).
  hasPlugins = SETTINGS.pluginsEnabled != 0;

  const auto& metrics = UITheme::getInstance().getMetrics();
  if (UITheme::getInstance().hasCoverGridHome()) {
    // Screen-lifetime interaction tables and component properties exceed the stack budget.
    coverGridUi = makeUniqueNoThrow<CoverGridHomeUi>(renderer);
    if (!coverGridUi) LOG_ERR("HOME", "OOM: cover grid UI; using standard home");
  }
  loadRecentBooks(coverGridUi ? CoverGridHomeUi::MAX_BOOKS : metrics.homeRecentBooksCount);
#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)
  carouselData.fill({});
  if (SETTINGS.uiTheme == CrossPointSettings::LYRA_CAROUSEL) {
    for (size_t i = 0; i < recentBooks.size() && i < carouselData.size(); ++i)
      carouselData[i] = loadCarouselBookData(recentBooks[i].path);
  }
#endif
  hasContinueReading = !recentBooks.empty();
  if (usesCompactHome()) loadDashboardData();
  if (coverGridUi) {
    fillCoverGridFromLibrary();
    resolveGridCoverPaths();
    coverGridUi->begin(recentBooks, hasLibrarySlot(), hasContinueReading, libraryFirstOnHome);
  }

  const auto base = static_cast<int>(recentBooks.size());
  selectorIndex = initialMenuItem == HomeMenuItem::NONE
                      ? 0
                      : base + menuItemToIndex(initialMenuItem, hasLibrarySlot(), libraryFirstOnHome);

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  coverGridUi.reset();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer || coverRectW <= 0 || coverRectH <= 0) return false;
  return renderer.copyBufferToRegion(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize);
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::loop() {
  if (!popup.isActive()) {
    if (statisticsOpenFailed) {
      statisticsOpenFailed = false;
      const char* options[] = {tr(STR_OK_BUTTON)};
      popup.showMessage(tr(STR_READING_STATISTICS), tr(STR_LIBRARY_STATS_OPEN_ERROR), options, 1, 0, [](int) {});
      requestUpdate();
      return;
    }
#if defined(CROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION)
    if (deferredMoveRetryPending && static_cast<int32_t>(millis() - deferredMoveRetryAt) >= 0) {
      deferredMoveRetryPending = false;
      ReadingStatsDateTime now{};
      const ReadingStatsDate today = getCurrentLocalReadingStatsDateTime(now) ? now.date : ReadingStatsDate{};
      const auto result = readingstats::deferredcompletion::executeOnePending(today, showDeferredMove, this);
      using MoveResult = readingstats::deferredcompletion::ExecuteMoveResult;
      switch (result) {
        case MoveResult::Moved:
          activityManager.goHome();
          return;
        case MoveResult::Cancelled:
          deferredMoveRetryPending = true;
          deferredMoveRetryAt = millis() + 500;
          break;
        case MoveResult::Missing:
          break;
        case MoveResult::Busy:
        case MoveResult::Blocked:
        case MoveResult::RecoveryRequired:
        case MoveResult::Unavailable:
          deferredMoveNeedsReview = true;
          break;
      }
      if (deferredMoveOverlayShown) {
        deferredMoveOverlayShown = false;
        requestUpdate();
      }
    }
    if (deferredMoveNeedsReview) {
      deferredMoveNeedsReview = false;
      const char* options[] = {tr(STR_RETRY), tr(STR_OK_BUTTON)};
      popup.showMessage(tr(STR_READING_STATISTICS), tr(STR_FINISHED_MOVE_REVIEW), options, 2, 1, [this](int choice) {
        if (choice == 0) {
          deferredMoveRetryPending = true;
          deferredMoveRetryAt = millis() + 500;
        }
      });
      requestUpdate();
      return;
    }
#endif
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
    const uint32_t stateFailure = stateSaveNotice().pending();
    if (stateFailure) {
      const char* options[] = {tr(STR_RETRY), tr(STR_OK_BUTTON)};
      popup.showMessage(tr(STR_STATE_SAVE_RECOVERY), tr(STR_STATE_RECOVERY_DETAILS), options, 2, 0,
                        [stateFailure](int index) {
                          if (index == 0 && !APP_STATE.recoverSleepStateForPageOverlay()) return;
                          stateSaveNotice().acknowledge(stateFailure);
                        });
      requestUpdate();
      return;
    }
#endif
    const uint32_t settingsFailure = settingsSaveNotice().pending();
    if (settingsFailure) {
      const char* options[] = {tr(STR_RETRY), tr(STR_OK_BUTTON)};
      popup.showMessage(tr(STR_SETTINGS_TITLE), tr(STR_SETTINGS_RECOVERY_DETAILS), options, 2, 0,
                        [settingsFailure](int index) {
                          if (index == 0 && !SETTINGS.saveToFile()) return;
                          settingsSaveNotice().acknowledge(settingsFailure);
                        });
      requestUpdate();
      return;
    }
    const uint32_t observed = statsSaveNotice().pending();
    if (observed) {
      const char* options[] = {tr(STR_OK_BUTTON)};
      popup.showMessage(tr(STR_READING_STATISTICS), tr(STR_READER_STATS_SAVE_FAILED), options, 1, 0,
                        [observed](int) { statsSaveNotice().acknowledge(observed); });
      requestUpdate();
      return;
    }
  }
  if (popup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  if (handleCompactHome()) return;
  const int menuCount = getMenuItemCount();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto page = menuPage();
#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)
  const bool carousel = SETTINGS.uiTheme == CrossPointSettings::LYRA_CAROUSEL;
  const int carouselBooks = static_cast<int>(recentBooks.size());
  if (carousel && selectorIndex >= 0 && selectorIndex < carouselBooks) carouselCenterIndex = selectorIndex;
#endif

  // Cover grid home splits navigation by button group (see below); the flat
  // next/previous cycle is for the classic list home only.
#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)
  if (carousel) {
    const auto move = [this, carouselBooks, menuCount](int step) {
      const bool inBooks = selectorIndex < carouselBooks;
      const int base = inBooks ? 0 : carouselBooks;
      const int count = inBooks ? carouselBooks : menuCount - carouselBooks;
      if (count <= 0) return;
      selectorIndex = base + (selectorIndex - base + count + step) % count;
      if (inBooks) carouselCenterIndex = selectorIndex;
      requestUpdate();
    };
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [&move] { move(-1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [&move] { move(1); });
    if (carouselBooks > 0 && (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
                              mappedInput.wasPressed(MappedInputManager::Button::Down))) {
      selectorIndex = selectorIndex < carouselBooks ? carouselBooks : carouselCenterIndex.load();
      requestUpdate();
    }
  }
#endif
  if (!coverGridUi
#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)
      && !carousel
#endif
  ) {
    buttonNavigator.onNext([this, menuCount] {
      selectorIndex = ButtonNavigator::nextIndex(selectorIndex, menuCount);
      requestUpdate();
    });

    buttonNavigator.onPrevious([this, menuCount] {
      selectorIndex = ButtonNavigator::previousIndex(selectorIndex, menuCount);
      requestUpdate();
    });
  }

#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)
  int swipeY = 0;
  const auto swipe = mappedInput.wasSwipe(nullptr, &swipeY);
  if (carousel && carouselBooks > 1 && swipeY >= metrics.homeTopPadding &&
      swipeY < metrics.homeTopPadding + homeCoverTileHeight() &&
      (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Right)) {
    const int step = swipe == MappedInputManager::SwipeDir::Left ? 1 : -1;
    carouselCenterIndex = (carouselCenterIndex + carouselBooks + step) % carouselBooks;
    selectorIndex = carouselCenterIndex;
    requestUpdate();
    return;
  }
  // A swipe belongs to the carousel even if it does not change selection.
  // In particular, it must not also trigger Home's synthetic Back action.
  if (carousel && swipe != MappedInputManager::SwipeDir::None) return;
#else
  const auto swipe = mappedInput.wasSwipe();
#endif
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const bool forward = swipe == MappedInputManager::SwipeDir::Up;
    if (coverGridUi) {
      selectorIndex = forward ? ButtonNavigator::nextIndex(selectorIndex, menuCount)
                              : ButtonNavigator::previousIndex(selectorIndex, menuCount);
    } else {
      const int bookRows = metrics.homeContinueReadingInMenu ? 0 : static_cast<int>(recentBooks.size());
      const int step = forward ? page.capacity : -page.capacity;
      selectorIndex = bookRows + std::clamp(page.first + step, 0, menuCount - bookRows - 1);
    }
    requestUpdate();
    return;
  }

  // Back is otherwise unused on the home menu: open the most recently read
  // book directly (recentBooks is most-recent-first and already pruned of
  // files missing from the SD card).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && hasContinueReading && !recentBooks.empty()) {
    onRecentBookSelected(recentBooks[0].path);
    return;
  }

  if (coverGridUi) {
    const int touched = coverGridUi->selectedAction(mappedInput);
    if (touched >= 0 && touched < menuCount) {
      selectorIndex = touched;
      activateSelection();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      activateSelection();
      return;
    }
    // Side page buttons walk the covers, front Left/Right walk the tabs
    // (selectorIndex is flat: books first, then the tab items). A press while
    // selection sits in the other band jumps into this band first.
    const int bookCount = static_cast<int>(recentBooks.size());
    const auto cycleBand = [this](const int base, const int count, const int dir) {
      if (count <= 0) return;
      int idx = selectorIndex - base;
      if (idx < 0 || idx >= count) {
        idx = dir > 0 ? 0 : count - 1;
      } else {
        idx = (idx + count + dir) % count;
      }
      selectorIndex = base + idx;
      requestUpdate();
    };
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up},
                                         [&cycleBand, bookCount] { cycleBand(0, bookCount, -1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down},
                                         [&cycleBand, bookCount] { cycleBand(0, bookCount, +1); });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this, &cycleBand] {
      const int bookCount = static_cast<int>(recentBooks.size());
      cycleBand(bookCount, getMenuItemCount() - bookCount, -1);
    });
    buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this, &cycleBand] {
      const int bookCount = static_cast<int>(recentBooks.size());
      cycleBand(bookCount, getMenuItemCount() - bookCount, +1);
    });
    return;
  }

  // Match the rendered cover grid, which lays tiles inside getContentArea()
  // (origin content.x + contentSidePadding, span content.width). Using
  // screen-relative getScreenWidth()/contentSidePadding here offset the touch
  // targets from the drawn covers by content.x on bezel-inset panels (EEGO A4).
  const Rect content = UITheme::getInstance().getContentArea(renderer);
#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)
  if (carousel && carouselBooks > 0) {
    int touchX = 0, touchY = 0;
    const int titleHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const auto layout =
        carousellayout::make(content.x, metrics.homeTopPadding, content.width, homeCoverTileHeight(), titleHeight);
    if (mappedInput.wasScreenTapped(touchX, touchY)) {
      const int target = carousellayout::hit(layout, touchX, touchY, carouselBooks, carouselCenterIndex);
      if (target >= 0) {
        const bool open = target == carouselCenterIndex && selectorIndex == target;
        carouselCenterIndex = target;
        selectorIndex = target;
        if (open) {
          haptic_feedback::touchAction();
          activateSelection();
        } else {
          requestUpdate();
        }
        return;
      }
    }
  }
#endif
  const int coverColumnCount = std::max(1, metrics.homeRecentBooksCount);
  const int recentCount =
#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)
      carousel ? 0 :
#endif
               std::min(static_cast<int>(recentBooks.size()), coverColumnCount);
  const int coverColumnWidth = (content.width - 2 * metrics.contentSidePadding) / coverColumnCount;
  int touchedBook = -1;
  const auto coverTouch = mappedInput.colTouch(
      touchedBook, content.x + metrics.contentSidePadding, coverColumnWidth, recentCount, metrics.homeTopPadding,
      std::min(metrics.homeTopPadding + homeCoverTileHeight(), page.top - GUI.getMenuTopInset()), coverColumnWidth);
  if (coverTouch != MappedInputManager::RowTouch::None) {
    if (coverTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedBook) {
        selectorIndex = touchedBook;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedBook;
      haptic_feedback::touchAction();
      activateSelection();
    }
    return;
  }

  const int menuTop = page.top;
#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)
  if (carousel) {
    int touchX = 0, touchY = 0;
    if (mappedInput.wasScreenTapped(touchX, touchY)) {
      const int target = carousellayout::menuHit(content.x, menuTop, content.width,
                                                 carousellayout::menuHeight(renderer.getLineHeight(SMALL_FONT_ID)),
                                                 page.count, touchX, touchY);
      if (target >= 0) {
        selectorIndex = page.first + target + static_cast<int>(recentBooks.size());
        haptic_feedback::touchAction();
        activateSelection();
        return;
      }
    }
  }
#endif
  const int renderedMenuCount = page.count;
  int menuRow = -1;
  // Row height from the theme, not the metrics table: RoundedRaff draws
  // font-derived rows and the touch grid must match the visuals exactly.
  const int menuRowHeight = GUI.getMenuRowHeight(renderer);
  const auto menuTouch = mappedInput.rowTouch(menuRow, menuTop, menuRowHeight + metrics.menuSpacing,
#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)
                                              carousel ? 0 : renderedMenuCount,
#else
                                              renderedMenuCount,
#endif
                                              0, INT32_MAX, menuRowHeight);
  if (menuTouch != MappedInputManager::RowTouch::None) {
    const int touchedIndex =
        page.first + menuRow + (metrics.homeContinueReadingInMenu ? 0 : static_cast<int>(recentBooks.size()));
    if (menuTouch == MappedInputManager::RowTouch::Down) {
      if (selectorIndex != touchedIndex) {
        selectorIndex = touchedIndex;
        requestUpdate();
      }
    } else {
      selectorIndex = touchedIndex;
      haptic_feedback::touchAction();
      activateSelection();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelection();
  }
}

void HomeActivity::render(RenderLock&&) {
  if (popup.isActive()) {
    renderer.clearScreen();
    if (popup.processRender(renderer, mappedInput)) return;
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  // Content columns clear the bezel insets on rounded panels. The header
  // self-insets in drawHeader, so only the cover/menu need it here.
  const Rect content = UITheme::getInstance().getContentArea(renderer);

  renderer.clearScreen();
  if (coverGridUi) {
    coverGridUi->setSelection(selectorIndex);
    UITheme::getInstance().drawCoverGridHome(*coverGridUi);
    // Front Left/Right walk the tabs, so their hints read Left/Right; the
    // side page buttons (unhinted) walk the covers.
    const auto labels = mappedInput.mapLabels(hasContinueReading ? tr(STR_RESUME) : "", tr(STR_SELECT),
                                              tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH
                                                                   : HalDisplay::FAST_REFRESH);
    // Slot heights are recorded during the draw above; a change (first layout
    // pass, orientation switch) means the paths must point at those sizes and
    // any missing thumbs must be generated. Refreshing the paths right away
    // lets the next pass draw already-cached thumbs before generation runs.
    const bool coverSpecChanged = coverGridUi->takeThumbHeightChanged();
    if (coverSpecChanged) {
      coverGridUi->refreshCoverPaths();
      recentsLoaded = false;
    }
    if (!firstRenderDone) {
      firstRenderDone = true;
      requestUpdate();
    } else if (!recentsLoaded && !recentsLoading) {
      loadRecentCovers(CoverGridHomeUi::THUMB_HEIGHT);
      coverGridUi->refreshCoverPaths();
      requestUpdate();
    }
    return;
  }
  const Rect tile{content.x, metrics.homeTopPadding, content.width, homeCoverTileHeight()};
  const Rect snapshot = !usesCompactHome()       ? tile
                        : GUI.hasDashboardHome() ? DashboardHomeTheme::coverRect(renderer, tile)
                                                 : MinimalHomeTheme::coverRect(renderer, tile);
  if (coverRectX != snapshot.x || coverRectY != snapshot.y || coverRectW != snapshot.width ||
      coverRectH != snapshot.height) {
    freeCoverBuffer();
    coverRendered = false;
    recentsLoaded = false;
  }
  bool bufferRestored = coverRendered && coverBufferStored && restoreCoverBuffer();

  // Band spans topPadding..homeTopPadding: the cover tile starts at the fixed
  // homeTopPadding, so the height must shrink by topPadding or the band (and a
  // centered title, e.g. RoundedRaff's book title) sinks into the tile.
  // Home is the stack root: no back button in its header.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding - metrics.topPadding},
                 metrics.homeContinueReadingInMenu && !recentBooks.empty() ? recentBooks[0].title.c_str() : nullptr,
                 nullptr, false);

  // Compact themes cache artwork only; their live text is drawn after restoration.
  coverRectX = snapshot.x;
  coverRectY = snapshot.y;
  coverRectW = snapshot.width;
  coverRectH = snapshot.height;

  GUI.drawRecentBookCover(renderer, Rect{content.x, metrics.homeTopPadding, content.width, homeCoverTileHeight()},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));
#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)
  if (SETTINGS.uiTheme == CrossPointSettings::LYRA_CAROUSEL && !recentBooks.empty()) {
    const int center = selectorIndex >= 0 && selectorIndex < static_cast<int>(recentBooks.size())
                           ? selectorIndex
                           : carouselCenterIndex.load();
    if (center >= 0 && static_cast<size_t>(center) < carouselData.size())
      LyraCarouselTheme::drawReadingProgress(
          renderer, Rect{content.x, metrics.homeTopPadding, content.width, homeCoverTileHeight()},
          carouselData[center]);
  }
#endif
  if (GUI.hasDashboardHome() && !recentBooks.empty()) {
    DashboardHomeTheme::drawReadingInfo(renderer,
                                        Rect{content.x, metrics.homeTopPadding, content.width, homeCoverTileHeight()},
                                        recentBooks.front(), dashboardData, dashboardDetails);
  }

  if (SETTINGS.uiTheme == CrossPointSettings::MINIMAL_HOME && !recentBooks.empty())
    MinimalHomeTheme::drawReadingInfo(
        renderer, Rect{content.x, metrics.homeTopPadding, content.width, homeCoverTileHeight()}, dashboardData);

  if (!usesCompactHome()) {
    // Build menu items dynamically
    std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_LIBRARY), tr(STR_SAVED_ITEMS),
                                          tr(STR_FILE_TRANSFER), tr(STR_SETTINGS_TITLE)};
    std::vector<UIIcon> menuIcons = {Folder, Library, Bookmark, Transfer, Settings};
    if (libraryFirstOnHome) {
      std::swap(menuItems[0], menuItems[1]);
      std::swap(menuIcons[0], menuIcons[1]);
    }

    if (hasLibrarySlot()) {
      menuItems.insert(menuItems.begin() + 3, hasPlugins ? tr(STR_PLUGINS) : tr(STR_OPDS_BROWSER));
      menuIcons.insert(menuIcons.begin() + 3, Plugins);
    }

    if (metrics.homeContinueReadingInMenu && !recentBooks.empty()) {
      // Insert Continue Reading at the top if enabled in theme
      menuItems.insert(menuItems.begin(), tr(STR_CONTINUE_READING));
      menuIcons.insert(menuIcons.begin(), Book);
    }

    const auto page = menuPage();
    const int inset = GUI.getMenuTopInset();
    renderer.fillRect(content.x, page.top - inset, content.width,
                      std::max(0, pageHeight - metrics.buttonHintsHeight - page.top + inset), false);
    const int bookRows = metrics.homeContinueReadingInMenu ? 0 : static_cast<int>(recentBooks.size());
#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)
    if (SETTINGS.uiTheme == CrossPointSettings::LYRA_CAROUSEL) {
      LyraCarouselTheme::drawHomeMenu(
          renderer,
          Rect{content.x, page.top, content.width, carousellayout::menuHeight(renderer.getLineHeight(SMALL_FONT_ID))},
          page.count, selectorIndex - bookRows, [&menuItems](int index) { return std::string(menuItems[index]); },
          [&menuIcons](int index) { return menuIcons[index]; });
    } else
#endif
      GUI.drawButtonMenu(
          renderer,
          Rect{content.x, page.top - inset, content.width,
               page.count * (GUI.getMenuRowHeight(renderer) + metrics.menuSpacing) + inset},
          page.count, selectorIndex - bookRows - page.first,
          [&menuItems, &page](int index) { return std::string(menuItems[page.first + index]); },
          [&menuIcons, &page](int index) { return menuIcons[page.first + index]; });

    const auto labels = mappedInput.mapLabels(recentBooks.empty() ? "" : tr(STR_RESUME), tr(STR_SELECT), tr(STR_DIR_UP),
                                              tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);

  if (!firstRenderDone) {
    firstRenderDone = true;
    requestUpdate();
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    const int themeThumbHeight = GUI.homeCoverThumbHeight(renderer);
    loadRecentCovers(themeThumbHeight > 0 ? themeThumbHeight : metrics.homeCoverHeight);
  }
}

void HomeActivity::onRecentBookSelected(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onLibraryOpen() { activityManager.goToLibrary(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onPluginsOpen() { activityManager.goToPlugins(hasOpdsServers); }
