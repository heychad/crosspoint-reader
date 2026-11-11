#include "RecentBooksStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

bool RecentBooksStore::saveForCompletionLocked() const {
  const std::string path = getFilePath();
  for (const char* suffix : {".tmp", ".bak"})
    if (Storage.lookupPath((path + suffix).c_str()) != HalStorage::PathStatus::Missing) return false;
  const auto status = Storage.lookupPath(path.c_str());
  if (status == HalStorage::PathStatus::Failed) return false;
  if (status == HalStorage::PathStatus::Present) {
    JsonDocument previous;
    if (!PersistableStoreBase::readDocFromFile(path.c_str(), previous, 65536) || !previous.is<JsonObject>())
      return false;
  }
  JsonDocument document;
  toJson(document);
  return PersistableStoreBase::writeDocToFileForMove(path.c_str(), document);
}

void RecentBooksStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& book : recentBooks) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = book.path;
    obj["title"] = book.title;
    obj["author"] = book.author;
    obj["coverBmpPath"] = book.coverBmpPath;
  }
}

bool RecentBooksStore::fromJson(JsonVariantConst doc) {
  // Tolerate a missing/invalid 'books' key (treat as empty list); only a
  // JSON parse error is fatal. A null JsonArray iterates zero times.
  recentBooks.clear();
  JsonArrayConst arr = doc["books"].as<JsonArrayConst>();
  recentBooks.reserve(std::min(arr.size(), static_cast<size_t>(MAX_RECENT_BOOKS)));
  for (JsonObjectConst obj : arr) {
    if (getCount() >= MAX_RECENT_BOOKS) break;
    RecentBook book;
    book.path = obj["path"] | "";
    book.title = obj["title"] | "";
    book.author = obj["author"] | "";
    book.coverBmpPath = obj["coverBmpPath"] | "";
    recentBooks.push_back(book);
  }

  LOG_DBG("RBS", "Recent books loaded from file (%d entries)", getCount());
  return true;
}

void RecentBooksStore::addBook(const std::string& path, const std::string& title, const std::string& author,
                               const std::string& coverBmpPath) {
  // Drop stale entries first so a new add can't evict a valid book in their stead.
  pruneMissing();

  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    // Move the existing entry to the front instead of copying it. The reader calls this after its first
    // render, so fresh string buffers would land among reader allocations and outlive them, splitting the
    // largest free block once the reader exits.
    std::rotate(recentBooks.begin(), it, it + 1);
    RecentBook& book = recentBooks.front();
    if (book.title != title) book.title = title;
    if (book.author != author) book.author = author;
    if (book.coverBmpPath != coverBmpPath) book.coverBmpPath = coverBmpPath;
  } else {
    recentBooks.insert(recentBooks.begin(), {path, title, author, coverBmpPath});
    if (recentBooks.size() > MAX_RECENT_BOOKS) {
      recentBooks.resize(MAX_RECENT_BOOKS);
    }
  }

  saveToFile();
}

void RecentBooksStore::updateBook(const std::string& path, const std::string& title, const std::string& author,
                                  const std::string& coverBmpPath) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != recentBooks.end()) {
    RecentBook& book = *it;
    book.title = title;
    book.author = author;
    book.coverBmpPath = coverBmpPath;
    saveToFile();
  }
}

bool RecentBooksStore::removeByPath(const std::string& path) {
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) {
    return false;
  }
  recentBooks.erase(it);
  if (!saveToFile()) {
    LOG_ERR("RBS", "Failed to persist removal of recent book: %s", path.c_str());
  }
  return true;
}

bool RecentBooksStore::removeByPathPersisted(const std::string& path) {
  // Completion can hold the storage lock while another store save holds this
  // mutex and waits for storage. Do not wait in the reverse lock order.
  std::unique_lock<std::mutex> lock(storeMutex, std::try_to_lock);
  if (!lock.owns_lock()) return false;
  auto it =
      std::find_if(recentBooks.begin(), recentBooks.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == recentBooks.end()) return true;
  const auto position = static_cast<size_t>(it - recentBooks.begin());
  auto removed = std::move(*it);
  recentBooks.erase(it);
  if (saveForCompletionLocked()) return true;
  // Erase retains vector capacity, so reinserting this moved entry does not
  // require growing the vector. On-disk outcome may still be uncertain.
  recentBooks.insert(recentBooks.begin() + position, std::move(removed));
  LOG_ERR("RBS", "Failed to persist removal of recent book: %s", path.c_str());
  return false;
}

void RecentBooksStore::updatePath(const std::string& oldPath, const std::string& newPath,
                                  const std::string& oldCachePath, const std::string& newCachePath) {
  auto it = std::find_if(recentBooks.begin(), recentBooks.end(),
                         [&](const RecentBook& book) { return book.path == oldPath; });
  if (it == recentBooks.end()) {
    return;
  }
  it->path = newPath;
  if (!oldCachePath.empty() && !it->coverBmpPath.empty() && it->coverBmpPath.rfind(oldCachePath, 0) == 0) {
    it->coverBmpPath = newCachePath + it->coverBmpPath.substr(oldCachePath.size());
  }
  saveToFile();
}

bool RecentBooksStore::updatePathPersisted(const std::string& oldPath, const std::string& newPath,
                                           const std::string& oldCachePath, const std::string& newCachePath) {
  std::unique_lock<std::mutex> lock(storeMutex, std::try_to_lock);
  if (!lock.owns_lock()) return false;
  if (oldPath == newPath) return true;
  auto old = std::find_if(recentBooks.begin(), recentBooks.end(),
                          [&](const RecentBook& book) { return book.path == oldPath; });
  if (old == recentBooks.end()) return true;
  const auto destination = std::find_if(recentBooks.begin(), recentBooks.end(),
                                        [&](const RecentBook& book) { return book.path == newPath; });
  if (destination != recentBooks.end()) return false;
  const std::string targetPath = newPath;
  auto savedPath = std::move(old->path);
  auto savedCover = std::move(old->coverBmpPath);
  old->path = targetPath;
  old->coverBmpPath = savedCover;
  if (!oldCachePath.empty() && !savedCover.empty() && savedCover.rfind(oldCachePath, 0) == 0)
    old->coverBmpPath = newCachePath + savedCover.substr(oldCachePath.size());
  if (saveForCompletionLocked()) return true;
  old->path = std::move(savedPath);
  old->coverBmpPath = std::move(savedCover);
  LOG_ERR("RBS", "Failed to persist recent book path: %s", old->path.c_str());
  return false;
}

bool RecentBooksStore::isMissing(const RecentBook& book) { return !Storage.exists(book.path.c_str()); }

bool RecentBooksStore::pruneMissing() {
  const size_t before = recentBooks.size();
  recentBooks.erase(std::remove_if(recentBooks.begin(), recentBooks.end(), &isMissing), recentBooks.end());
  return recentBooks.size() != before;
}
