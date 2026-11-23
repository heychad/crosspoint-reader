#include "BookmarkFile.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include "BookmarkReadPath.h"
#include "activities/reader/StatsAccessGate.h"

namespace {
// Preserve the SD card reader's existing size limit.
constexpr size_t MAX_DOCUMENT_BYTES = 50000;
using CheckedRead = PersistableStoreBase::CheckedRead;

bool incompleteFirstStage(NativeBookmarkSource source, CheckedRead read, const std::string& path) {
  return source == NativeBookmarkSource::Staged && read == CheckedRead::Malformed &&
         Storage.moveChainEligible(path.c_str(), /*allowUnallocatedEmpty=*/true);
}

JsonArrayConst bookmarkRows(const JsonDocument& doc) {
  const auto rows = doc["bookmarks"].as<JsonArrayConst>();
  for (const auto row : rows) {
    if (!row.is<JsonObjectConst>()) return {};
  }
  return rows;
}

struct BookmarkLoad {
  std::string path;
  std::vector<BookmarkEntry>& entries;
  BookmarkFile::LoadResult result = BookmarkFile::LoadResult::Unavailable;
};

bool readBookmarks(void* raw) {
  auto& load = *static_cast<BookmarkLoad*>(raw);
  const auto status = selectNativeBookmarkFile(load.path);
  if (status == NativeBookmarkSource::Unavailable) return false;
  JsonDocument doc;
  const auto read = status == NativeBookmarkSource::Missing
                        ? CheckedRead::Unavailable
                        : PersistableStoreBase::readCheckedDocFromFile(load.path.c_str(), doc, MAX_DOCUMENT_BYTES);
  if (status == NativeBookmarkSource::Missing || incompleteFirstStage(status, read, load.path)) {
    load.entries.clear();
    load.result = BookmarkFile::LoadResult::Missing;
    return true;
  }
  if (read != CheckedRead::Loaded) return false;
  const auto arr = bookmarkRows(doc);
  if (arr.isNull()) return false;

  auto& bookmarks = load.entries;
  bookmarks.clear();
  bookmarks.reserve(arr.size());
  for (const auto obj : arr) {
    bookmarks.emplace_back();
    auto& bookmark = bookmarks.back();
    bookmark.xpath = obj["xpath"] | "";
    bookmark.percentage = obj["percentage"] | static_cast<float>(0);
    bookmark.summary = obj["summary"] | "";
    bookmark.name = obj["name"] | "";
    if (bookmark.name.size() > BookmarkEntry::MAX_NAME_LENGTH) {
      bookmark.name.resize(BookmarkEntry::MAX_NAME_LENGTH);
    }
    bookmark.computedSpineIndex = obj["si"] | static_cast<uint16_t>(0);
    bookmark.computedChapterPageCount = obj["pc"] | static_cast<uint16_t>(0);
    bookmark.computedChapterProgress = obj["pp"] | static_cast<uint16_t>(0);
    if (!obj["vo"].isNull()) {
      bookmark.visibleTextOffset = obj["vo"] | static_cast<uint32_t>(0);
      bookmark.hasVisibleTextOffset = true;
    }
  }

  load.result = BookmarkFile::LoadResult::Loaded;
  LOG_DBG("BKM", "Loaded %zu bookmarks from file", bookmarks.size());
  return true;
}
}  // namespace

BookmarkFile::LoadResult BookmarkFile::load(const std::string& bookPath, std::vector<BookmarkEntry>& bookmarks) {
  BookmarkLoad job{BookmarkUtil::getBookmarkPath(bookPath), bookmarks};
  return Storage.withStorageLock(readBookmarks, &job) ? job.result : LoadResult::Unavailable;
}

namespace {
struct BookmarkSave {
  const std::string& bookPath;
  const std::vector<BookmarkEntry>& bookmarks;
  BookmarkFile::Omit omit;
};

bool saveBookmarks(void* raw) {
  const auto& save = *static_cast<BookmarkSave*>(raw);
  const std::string path = BookmarkUtil::getBookmarkPath(save.bookPath);
  auto currentPath = path;
  const auto source = selectNativeBookmarkFile(currentPath);
  if (source == NativeBookmarkSource::Unavailable) return false;
  // An unfinished move can leave old and new bookmark names on one chain.
  if (!readingstats::normalStatsAllowed()) return false;
  if (source == NativeBookmarkSource::Staged) {
    // There is no committed copy. A complete stage still goes through the
    // writer's normal recovery. Only a proved incomplete stage can be retired.
    JsonDocument staged;
    const auto read = PersistableStoreBase::readCheckedDocFromFile(currentPath.c_str(), staged, MAX_DOCUMENT_BYTES);
    if (read == CheckedRead::Loaded && bookmarkRows(staged).isNull()) return false;
    if (read != CheckedRead::Loaded &&
        (!incompleteFirstStage(source, read, currentPath) || !Storage.remove(currentPath.c_str()) ||
         Storage.lookupPath(currentPath.c_str()) != HalStorage::PathStatus::Missing))
      return false;
  }

  JsonDocument doc;
  JsonArray arr = doc["bookmarks"].to<JsonArray>();
  for (const auto& bookmark : save.bookmarks) {
    if (save.omit.matches && save.omit.matches(bookmark, save.omit.context)) continue;
    if (bookmark.name.size() > BookmarkEntry::MAX_NAME_LENGTH) {
      LOG_ERR("BKM", "Bookmark name exceeds %zu bytes", BookmarkEntry::MAX_NAME_LENGTH);
      return false;
    }
    JsonObject obj = arr.add<JsonObject>();
    obj["xpath"] = bookmark.xpath;
    obj["percentage"] = bookmark.percentage;
    obj["summary"] = bookmark.summary;
    if (!bookmark.name.empty()) {
      obj["name"] = bookmark.name;
    }
    obj["si"] = bookmark.computedSpineIndex;
    obj["pc"] = bookmark.computedChapterPageCount;
    obj["pp"] = bookmark.computedChapterProgress;
    if (bookmark.hasVisibleTextOffset) {
      obj["vo"] = bookmark.visibleTextOffset;
    }
  }

  Storage.mkdir(BookmarkUtil::getBookmarksDir().c_str(), true);
  const auto temporary = BookmarkUtil::nativeRecoveryPath(path, false);
  const auto backup = BookmarkUtil::nativeRecoveryPath(path, true);
  return PersistableStoreBase::writeDocToFileAtomic(path.c_str(), temporary.c_str(), backup.c_str(), doc,
                                                    MAX_DOCUMENT_BYTES);
}
}  // namespace

bool BookmarkFile::save(const std::string& bookPath, const std::vector<BookmarkEntry>& bookmarks, Omit omit) {
  BookmarkSave request{bookPath, bookmarks, omit};
  return Storage.withStorageLock(saveBookmarks, &request);
}
