#pragma once

#include <BookReadingStats.h>
#include <DeferredCompletionMove.h>
#include <GlobalReadingStats.h>
#include <HalClock.h>
#include <ReaderStats.h>
#include <ReadingStatsStore.h>
#include <RecentBooksStore.h>
#if defined(CROSSPOINT_EXPERIMENTAL_DAILY_STATS)
#include <DailyReadingStatsStore.h>
#endif

#include "MovedReadingState.h"

namespace livecompletiontest {
inline void clockAt(unsigned seconds) {
  static unsigned previousSeconds = 0;
  liveTestClockEnabled = true;
  if (seconds == 0) {
    liveTestMillis = 1;
    previousSeconds = 0;
  } else {
    assert(seconds >= previousSeconds);
    liveTestMillis += (seconds - previousSeconds) * 1000;
    previousSeconds = seconds;
  }
  dailyClockEnabled = true;
  dailyClock = {};
  dailyClock.tm_year = 126;
  dailyClock.tm_mon = 10;
  dailyClock.tm_mday = 12;
  dailyClock.tm_hour = 12;
  dailyClock.tm_min = seconds / 60;
  dailyClock.tm_sec = seconds % 60;
}

struct RecentsPolicy {
  bool failOnce;
  unsigned calls = 0;
};
inline bool removeRecent(const std::string& path, void* raw) {
  auto& policy = *static_cast<RecentsPolicy*>(raw);
  ++policy.calls;
  if (policy.failOnce) {
    policy.failOnce = false;
    return false;
  }
  return RECENT_BOOKS.removeByPathPersisted(path);
}

template <class Device>
void exercise(Device& disk, bool exfat, bool removeFromRecents, bool undo, bool failRecents = false) {
  namespace dc = readingstats::deferredcompletion;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter formatter;
    assert(formatter.format(&disk, scratch));
  } else {
    FatFormatter formatter;
    assert(formatter.format(&disk, scratch));
  }
  constexpr char source[] = "/books/live.epub";
  constexpr char destination[] = "/read/live.epub";
  const auto cache = movedstate::cachePath(source);
  const auto directory = readingstats::bookDirectory(cache);
  const ReadingStatsDate today{2026, 11, 12};
  movedstate::ClippingSnapshot clipping;
  {
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    assert(Storage.mkdir("/books", true) && Storage.mkdir("/read", true));
    auto bookFile = Storage.open(source, O_CREAT | O_EXCL | O_WRONLY);
    const uint8_t bytes[] = {1, 3, 5, 7};
    assert(bookFile && bookFile.write(bytes, sizeof(bytes)) == sizeof(bytes) && bookFile.sync() && bookFile.close());
    movedstate::seedProgress(source, false, movedstate::ProgressFormat::TextOffset);
    movedstate::seedBookmarks(source, 4);
    movedstate::seedNativeBookmarks(source);
    clipping = movedstate::seedClippings(source);
    movedstate::seedSidecars(source);
    BookReadingStats book;
    book.totalReadingSeconds = 1234;
    assert(readingstats::saveBook(directory, book));
    GlobalReadingStats global;
    global.totalReadingSeconds = 5678;
    assert(global.save());
    JsonDocument state, recent;
    state["openEpubPath"] = source;
    state["favoriteSleepImagePath"] = "/favorite.bmp";
    assert(APP_STATE.fromJson(state.as<JsonVariantConst>()) && APP_STATE.saveToFile());
    auto row = recent["books"].to<JsonArray>().add<JsonObject>();
    row["path"] = source;
    row["title"] = "Book";
    row["author"] = "Writer";
    recent["books"].add<JsonObject>()["path"] = "/other.epub";
    assert(RECENT_BOOKS.fromJson(recent.as<JsonVariantConst>()) && RECENT_BOOKS.saveToFile());
    assert(!StatsSessionLease::anyActive());
    RecentsPolicy policy{failRecents};
    unsigned notices = 0;
    const auto notice = [](void* raw) { ++*static_cast<unsigned*>(raw); };
    {
      ReaderStats reader;
      clockAt(0);
      reader.begin(cache, source, true, 300, true);
      assert(!reader.unavailableAtStart() && StatsSessionLease::anyActive());
      reader.onRender(ReadingStatsFrame::Page);
      clockAt(65);
      reader.noteTurn(true, true);
      reader.onRender(ReadingStatsFrame::Page);
      if (failRecents) {
        clockAt(130);
        reader.noteTurn(true, true);
        reader.onRender(ReadingStatsFrame::Page);
      }
      const auto marked =
          reader.editCompletionStats(cache, source, true, today, removeFromRecents, removeRecent, &policy, true);
      assert(marked ==
             (failRecents ? ReaderStats::CompletionResult::RecoveryRequired : ReaderStats::CompletionResult::Applied));
      if (!failRecents) {
        assert(dc::executeOnePending(today, notice, &notices) == dc::ExecuteMoveResult::Busy && notices == 0);
        if (undo)
          assert(reader.editCompletionStats(cache, source, false, today) == ReaderStats::CompletionResult::Applied);
        reader.onRender(ReadingStatsFrame::Page);
        clockAt(130);
        reader.noteTurn(true, true);
        reader.onRender(ReadingStatsFrame::Page);
      }
      assert(reader.finish());
      assert(Storage.lookupPath(source) == HalStorage::PathStatus::Present &&
             Storage.lookupPath(destination) == HalStorage::PathStatus::Missing);
      // onExit/finish alone cannot release retained reader ownership.
      if (!failRecents)
        assert(StatsSessionLease::anyActive() &&
               dc::executeOnePending(today, notice, &notices) == dc::ExecuteMoveResult::Busy && notices == 0);
    }
    assert(!StatsSessionLease::anyActive());
    const auto executed = dc::executeOnePending(today, notice, &notices);
    if (executed != (undo ? dc::ExecuteMoveResult::Cancelled : dc::ExecuteMoveResult::Moved)) {
      readingstats::completionjournal::Record pending{};
      size_t size = 0;
      unsigned slot = 0;
      const auto state = readingstats::completionjournal::scanRecords(pending, size, slot);
      std::fprintf(stderr,
                   "live exfat=%d remove=%d undo=%d fail=%d result=%d notices=%u journal=%d slot=%u policy=%u "
                   "source=%d target=%d assess=%d\n",
                   exfat, removeFromRecents, undo, failRecents, static_cast<int>(executed), notices,
                   static_cast<int>(state), slot, pending[9], static_cast<int>(Storage.lookupPath(source)),
                   static_cast<int>(Storage.lookupPath(destination)), static_cast<int>(dc::assess(source)));
      readingstats::completionmove::Plan plan;
      size_t planSize = 0;
      std::string_view from, to;
      const auto inspected = readingstats::completionmove::inspectPlan(plan, planSize, from, to);
      std::fprintf(stderr, "live plan=%d\n", static_cast<int>(inspected));
    }
    assert(executed == (undo ? dc::ExecuteMoveResult::Cancelled : dc::ExecuteMoveResult::Moved));
    assert(notices == 1 && dc::executeOnePending(today, notice, &notices) == dc::ExecuteMoveResult::Missing);
    assert(notices == 1 && policy.calls == (removeFromRecents ? 1U : 0U));
    assert(APP_STATE.openEpubPath == (undo ? source : destination));
    assert(RECENT_BOOKS.getCount() == (removeFromRecents ? 1U : 2U));
  }
  {
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    const std::string finalPath = undo ? source : destination;
    const auto finalCache = movedstate::cachePath(finalPath);
    const auto book = BookReadingStats::load(readingstats::bookDirectory(finalCache));
    const auto global = GlobalReadingStats::loadReadOnly();
    assert(book.writable && book.isCompleted == !undo && book.totalReadingSeconds == 1364 && book.sessionCount == 1);
    assert(global.writable && global.totalReadingSeconds == 5808 && global.totalSessions == 1 &&
           global.completedBooks == (undo ? 0U : 1U));
    movedstate::checkProgress(finalPath, movedstate::ProgressFormat::TextOffset);
    movedstate::checkBookmarks(finalPath, 4);
    movedstate::checkNativeBookmarks(finalPath);
    movedstate::checkClippings(finalPath, clipping);
    movedstate::checkSavedKinds(finalPath);
    movedstate::checkSidecars(finalPath);
    ReaderStats reopened;
    reopened.begin(finalCache, finalPath, true, 300, true);
    bool completed = false;
    assert(!reopened.unavailableAtStart() && reopened.completionStatus(completed) && completed == !undo);
    assert(reopened.finish());
#if defined(CROSSPOINT_EXPERIMENTAL_DAILY_STATS)
    dailyreading::Counter day;
    assert(dailyreading::read(readingStatsDayIndex(today), day) == dailyreading::ReadResult::Available);
    assert(day.seconds == 130 && day.uploaded == 0);
#endif
  }
  dailyClockEnabled = false;
  liveTestClockEnabled = false;
}
}  // namespace livecompletiontest
