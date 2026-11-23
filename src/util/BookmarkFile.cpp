#include "BookmarkFile.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include "BookmarkReadPath.h"

namespace {
// Preserve the SD card reader's existing size limit while checking for short reads.
constexpr size_t MAX_READ_BYTES = 50000;

struct BookmarkLoad {
  std::string path;
  std::vector<BookmarkEntry>& entries;
  BookmarkFile::LoadResult result = BookmarkFile::LoadResult::Unavailable;
};

bool readBookmarks(void* raw) {
  auto& load = *static_cast<BookmarkLoad*>(raw);
  const auto status = selectNativeBookmarkFile(load.path);
  if (status == HalStorage::PathStatus::Missing) {
    load.entries.clear();
    load.result = BookmarkFile::LoadResult::Missing;
    return true;
  }
  if (status != HalStorage::PathStatus::Present) return false;
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(load.path.c_str(), doc, MAX_READ_BYTES)) return false;
  const auto arr = doc["bookmarks"].as<JsonArrayConst>();
  if (arr.isNull()) return false;
  for (const auto entry : arr) {
    if (!entry.is<JsonObjectConst>()) return false;
  }

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

bool BookmarkFile::save(const std::string& bookPath, const std::vector<BookmarkEntry>& bookmarks, Omit omit) {
  JsonDocument doc;
  JsonArray arr = doc["bookmarks"].to<JsonArray>();
  for (const auto& bookmark : bookmarks) {
    if (omit.matches && omit.matches(bookmark, omit.context)) continue;
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
  const std::string path = BookmarkUtil::getBookmarkPath(bookPath);
  const auto temporary = BookmarkUtil::nativeRecoveryPath(path, false);
  const auto backup = BookmarkUtil::nativeRecoveryPath(path, true);
  return PersistableStoreBase::writeDocToFileAtomic(path.c_str(), temporary.c_str(), backup.c_str(), doc,
                                                    MAX_READ_BYTES);
}
