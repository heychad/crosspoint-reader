#pragma once

#include <BookTracking.h>
#include <Epub/ReferencePageMap.h>
#include <Logging.h>
#include <Memory.h>
#include <ReadingStatsUtils.h>
#include <personalization/CompletionPrompt.h>
#include <util/ReadFolder.h>

#include <atomic>
#include <functional>
#include <limits>
#include <optional>

#include "LiveCompletion.h"

namespace completionuitest {
enum {
  STR_COMPLETION_PROMPT_TITLE,
  STR_COMPLETION_PROMPT_BODY,
  STR_COMPLETION_RETRY_TITLE,
  STR_COMPLETION_RETRY_BODY,
  STR_CANCEL,
  STR_RETRY,
  STR_OK_BUTTON,
  STR_COMPLETION_MOVE_UNSUPPORTED_TITLE,
  STR_COMPLETION_MOVE_UNSUPPORTED_BODY
};
inline const char* tr(int) { return "test"; }
inline bool renderHeld = false;
struct RenderLock {
  RenderLock() {
    assert(!renderHeld);
    renderHeld = true;
  }
  ~RenderLock() {
    assert(renderHeld);
    renderHeld = false;
  }
};
struct Settings {
  bool moveFinishedToReadFolder = true, removeReadBooksFromRecents = false;
};
inline Settings SETTINGS;
namespace FsHelpers {
inline bool hasEpubExtension(const std::string& path) { return path.ends_with(".epub"); }
}  // namespace FsHelpers
namespace bleturner {
inline void acknowledgeLinkNote() {}
}  // namespace bleturner
struct Txt {
  static bool isTxtOrMd(const std::string& path) { return path.ends_with(".txt") || path.ends_with(".md"); }
};
struct Renderer {};
struct Input {};
struct ConfirmationActivity {
  ConfirmationActivity(Renderer&, Input&, const char*, const char*, const char* = "", const char* = "", bool = false) {}
};
struct ActivityResult {
  bool isCancelled = false;
};
struct Metadata {
  std::string path, manifest;
  const std::string& getPath() const { return path; }
  size_t getBookSize() const { return 1000; }
  int getSpineIndexForSize(size_t) const { return 1; }
  size_t getCumulativeSpineItemSize(int index) const { return index == 0 ? 500 : 1000; }
  int getSpineItemsCount() const { return 2; }
  bool getItemSize(const char*, size_t* size) const {
    *size = manifest.size();
    return !manifest.empty();
  }
  bool readItemContentsToStream(const char*, Print& out, size_t) const {
    return out.write(reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size()) == manifest.size();
  }
};
struct Section {
  int currentPage = 0, pageCount = 10;
  int estimatedTotalPages() const { return pageCount; }
  bool isBuilding() const { return false; }
  bool isPartial() const { return false; }
};

class ReaderActivity {
 public:
  std::string bookPath, cache;
  std::atomic<bool> pageRendered{true};
  ReaderStats readingStats;
  Renderer renderer;
  Input mappedInput;
  unsigned dialogs = 0, updates = 0;
  std::function<void(const ActivityResult&)> callback;
  ReaderActivity(const std::string& path, const std::string& cachePath) : bookPath(path), cache(cachePath) {}
  const std::string& getBookCachePath() const { return cache; }
  void requestUpdate() { ++updates; }
  void startActivityForResult(std::unique_ptr<ConfirmationActivity>, std::function<void(const ActivityResult&)> next) {
    assert(!renderHeld);
    ++dialogs;
    callback = std::move(next);
  }
  void respond(bool cancel) {
    assert(callback && !renderHeld);
    auto next = std::move(callback);
    callback = {};
    next({cancel});
  }
  ReaderStats::CompletionResult setBookCompleted(bool completed);
  ReaderStats::CompletionResult recoverBookCompletion();
  void handleCompletionAttempt(bool recovery, bool completed = true);
};

class EpubReaderActivity : public ReaderActivity {
 public:
  CompletionPrompt completionPrompt;
  std::shared_ptr<Metadata> epub = std::make_shared<Metadata>();
  std::unique_ptr<Section> section = std::make_unique<Section>();
  int currentSpineIndex = 1, footnoteDepth = 0, pendingManualTurn = 0, nextPageNumber = 0;
  bool pendingManualTurnTouch = false;
  unsigned long lastPageTurnTime = 0;
  std::optional<uint16_t> pendingPageJump;
  EpubReaderActivity(const std::string& path, const std::string& cachePath) : ReaderActivity(path, cachePath) {
    epub->path = path;
  }
  bool inputOverPage() const { return false; }
  void clearDeferredReposition() {}
  void initializeCompletionPrompt();
  void observeCompletionPosition();
  bool showQueuedCompletionPrompt();
  bool pageTurn(bool forward);
};

// Unmodified production methods, with only their activity/metadata hosts replaced.
#include <PromptCompletionMethods.h>

enum class Choice { Confirm, Cancel, Retry };
enum class Tracking { On, GlobalOff, BookOff };

template <class Device>
void exercise(Device& disk, bool exfat, bool removeRecents, Choice choice, Tracking tracking) {
  namespace dc = readingstats::deferredcompletion;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter formatter;
    assert(formatter.format(&disk, scratch));
  } else {
    FatFormatter formatter;
    assert(formatter.format(&disk, scratch));
  }
  constexpr char source[] = "/books/prompt.epub", target[] = "/read/prompt.epub";
  const auto cache = movedstate::cachePath(source);
  const ReadingStatsDate today{2026, 11, 12};
  movedstate::ClippingSnapshot clipping;
  const bool accepted = choice != Choice::Cancel;
  const bool tracked = tracking == Tracking::On;
  {
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    assert(Storage.mkdir("/books", true));
    auto file = Storage.open(source, O_CREAT | O_EXCL | O_WRONLY);
    const uint8_t bytes[] = {1, 3, 5, 7};
    assert(file && file.write(bytes, sizeof(bytes)) == sizeof(bytes) && file.sync() && file.close());
    movedstate::seedProgress(source, false, movedstate::ProgressFormat::TextOffset);
    movedstate::seedBookmarks(source, 4);
    movedstate::seedNativeBookmarks(source);
    clipping = movedstate::seedClippings(source);
    movedstate::seedSidecars(source);
    BookReadingStats book;
    book.totalReadingSeconds = 1234;
    assert(readingstats::saveBook(readingstats::bookDirectory(cache), book));
    GlobalReadingStats global;
    global.totalReadingSeconds = 5678;
    assert(global.save());
    if (tracking == Tracking::BookOff) assert(booktracking::setEnabled(cache, source, false));
    JsonDocument state, recent;
    state["openEpubPath"] = source;
    assert(APP_STATE.fromJson(state.as<JsonVariantConst>()) && APP_STATE.saveToFile());
    recent["books"].to<JsonArray>().add<JsonObject>()["path"] = source;
    recent["books"].add<JsonObject>()["path"] = "/other.epub";
    assert(RECENT_BOOKS.fromJson(recent.as<JsonVariantConst>()) && RECENT_BOOKS.saveToFile());
    SETTINGS = {true, removeRecents};
    unsigned notices = 0;
    const auto notice = [](void* raw) { ++*static_cast<unsigned*>(raw); };
    {
      livecompletiontest::clockAt(0);
      EpubReaderActivity reader(source, cache);
      reader.readingStats.begin(cache, source, tracking != Tracking::GlobalOff, 300, true);
      assert(!reader.readingStats.unavailableAtStart() && StatsSessionLease::anyActive());
      reader.readingStats.onRender(ReadingStatsFrame::Page);
      if (removeRecents) {
        reader.epub->manifest =
            R"({"format":"x-locations","version":1,"totalLocations":1000,"totalCharacters":1000,"charactersPerReferencePage":100,"spine":[{"characterStart":0,"characterCount":995},{"characterStart":995,"characterCount":5}]})";
        reader.currentSpineIndex = 0;
      }
      reader.initializeCompletionPrompt();
      reader.observeCompletionPosition();
      livecompletiontest::clockAt(65);
      reader.readingStats.noteTurn(true, true);
      reader.readingStats.onRender(ReadingStatsFrame::Page);
      reader.section->currentPage = 9;
      assert(!reader.pageTurn(true) && reader.completionPrompt.queued());
      assert(reader.showQueuedCompletionPrompt() && reader.dialogs == 1 && !renderHeld);
      std::string blocker;
      if (choice == Choice::Retry) {
        blocker = removeRecents ? std::string(RecentBooksStore::getFilePath()) + ".tmp"
                                : readingstats::bookDirectory(cache) + "/stats_v5.bin.tmp";
        auto bad = Storage.open(blocker.c_str(), O_CREAT | O_EXCL | O_WRONLY);
        assert(bad && bad.write(uint8_t{0xff}) == 1 && bad.sync() && bad.close());
      }
      reader.respond(choice == Choice::Cancel);
      if (choice == Choice::Retry) {
        assert(reader.dialogs == 2 && Storage.lookupPath(source) == HalStorage::PathStatus::Present);
        if (removeRecents) assert(reader.readingStats.unavailableAtStart() && RECENT_BOOKS.getCount() == 2);
        reader.respond(false);
        assert(reader.dialogs == 3 && Storage.lookupPath(blocker.c_str()) == HalStorage::PathStatus::Present);
        assert(Storage.remove(blocker.c_str()));
        reader.respond(false);
        assert(reader.dialogs == 3 && !reader.readingStats.unavailableAtStart());
      }
      bool completed = false;
      assert(reader.readingStats.completionStatus(completed) && completed == accepted);
      assert(!reader.showQueuedCompletionPrompt() && reader.pageTurn(true));
      assert(dc::executeOnePending(today, notice, &notices) == dc::ExecuteMoveResult::Busy && notices == 0);
      assert(reader.readingStats.finish() && StatsSessionLease::anyActive());
      assert(dc::executeOnePending(today, notice, &notices) == dc::ExecuteMoveResult::Busy && notices == 0);
    }
    assert(!StatsSessionLease::anyActive() && !renderHeld);
    const auto moved = dc::executeOnePending(today, notice, &notices);
    assert(moved == (accepted ? dc::ExecuteMoveResult::Moved : dc::ExecuteMoveResult::Missing));
    assert(notices == (accepted ? 1U : 0U));
    assert(dc::executeOnePending(today, notice, &notices) == dc::ExecuteMoveResult::Missing);
  }
  {
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    const std::string finalPath = accepted ? target : source;
    const auto finalCache = movedstate::cachePath(finalPath);
    assert(Storage.lookupPath(finalPath.c_str()) == HalStorage::PathStatus::Present &&
           Storage.lookupPath(accepted ? source : target) == HalStorage::PathStatus::Missing);
    const auto book = BookReadingStats::load(readingstats::bookDirectory(finalCache));
    const auto global = GlobalReadingStats::loadReadOnly();
    assert(book.writable && book.isCompleted == accepted && book.totalReadingSeconds == 1234 + (tracked ? 65 : 0));
    assert(book.sessionCount == (tracked ? 1U : 0U));
    assert(global.writable && global.completedBooks == (accepted ? 1U : 0U) &&
           global.totalReadingSeconds == 5678 + (tracked ? 65 : 0) && global.totalSessions == (tracked ? 1U : 0U));
    assert(booktracking::state(finalCache, finalPath) ==
           (tracking == Tracking::BookOff ? booktracking::State::Disabled : booktracking::State::Enabled));
    assert(APP_STATE.loadFromFile() && APP_STATE.openEpubPath == finalPath);
    assert(RECENT_BOOKS.loadFromFile() && RECENT_BOOKS.getCount() == (accepted && removeRecents ? 1U : 2U));
    assert(RECENT_BOOKS.getBooks().front().path == (accepted && removeRecents ? "/other.epub" : finalPath));
    movedstate::checkProgress(finalPath, movedstate::ProgressFormat::TextOffset);
    movedstate::checkBookmarks(finalPath, 4);
    movedstate::checkNativeBookmarks(finalPath);
    movedstate::checkClippings(finalPath, clipping);
    movedstate::checkSavedKinds(finalPath);
    movedstate::checkSidecars(finalPath);
#if defined(CROSSPOINT_EXPERIMENTAL_DAILY_STATS)
    dailyreading::Counter day;
    const auto daily = dailyreading::read(readingStatsDayIndex(today), day);
    assert(daily == (tracked ? dailyreading::ReadResult::Available : dailyreading::ReadResult::Missing));
    if (tracked) assert(day.seconds == 65 && day.uploaded == 0);
#endif
  }
  dailyClockEnabled = liveTestClockEnabled = false;
}
}  // namespace completionuitest
