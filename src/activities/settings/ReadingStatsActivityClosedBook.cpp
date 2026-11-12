#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Xtc.h>

#include "ReadingStatsActivity.h"
#include "activities/reader/ReadingStatsStore.h"
#include "util/BookProgress.h"

std::unique_ptr<ReadingStatsActivity> ReadingStatsActivity::forClosedBook(GfxRenderer& renderer,
                                                                          MappedInputManager& mappedInput,
                                                                          const std::string& path,
                                                                          const std::string& title) {
  if (Storage.lookupPath(path.c_str()) != HalStorage::PathStatus::Present) {
    LOG_ERR("STATS", "Cannot open statistics for a missing or unreadable book");
    return {};
  }
  std::string cache;
  if (FsHelpers::hasReflowableBookExtension(path)) {
    cache = Epub::cachePathForFilePath(path, "/.crosspoint");
  } else if (FsHelpers::hasXtcExtension(path)) {
    auto xtc = makeUniqueNoThrow<Xtc>(path, "/.crosspoint");
    if (xtc) cache = xtc->getCachePath();
  }
  struct Snapshot {
    BookReadingStats book;
    GlobalReadingStats global;
  };
  static_assert(sizeof(Snapshot) <= 512, "Review closed-book statistics working memory");
  auto snapshot = makeUniqueNoThrow<Snapshot>();
  if (cache.empty() || !snapshot) {
    LOG_ERR("STATS", "Cannot prepare book statistics");
    return {};
  }
  bool tracking = false;
  bool found = false;
  bool trackingKnown = false;
  snapshot->book =
      readingstats::loadBook(cache, path, tracking, &found, BookReadingStats::LoadMode::ReadOnly, &trackingKnown);
  if (!found || !trackingKnown) snapshot->book.writable = false;
  snapshot->global = GlobalReadingStats::loadReadOnly();
  int progress = -1;
  // TXT/Markdown validation can remove stale caches; leave progress unknown.
  if (FsHelpers::hasEpubExtension(path) || FsHelpers::hasXtcExtension(path)) {
    const int percent = loadBookProgress(path);
    if (percent >= 0) progress = percent * 100;
  }
  auto screen = makeUniqueNoThrow<ReadingStatsActivity>(renderer, mappedInput, title, snapshot->book, snapshot->global,
                                                        progress, tracking, true, cache, path, nullptr, true);
  snapshot.reset();
  return screen;
}
