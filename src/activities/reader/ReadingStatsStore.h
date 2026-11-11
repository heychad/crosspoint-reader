#pragma once

#include <string>

#include "BookReadingStats.h"
#include "StatsSessionLease.h"

namespace readingstats {
// Separate from rebuildable page caches; the filename still uses the existing book identity.
std::string bookDirectory(const std::string& cachePath);
BookReadingStats loadBook(const std::string& cachePath, const std::string& bookPath, bool& enabled,
                          bool* found = nullptr, BookReadingStats::LoadMode mode = BookReadingStats::LoadMode::Repair,
                          bool* trackingKnown = nullptr);
bool preserveCache(const std::string& cachePath);
// Clear rebuildable data even when statistics cannot be migrated.
bool clearCache(const std::string& cachePath);
// Clear known rebuildable EPUB caches without removing reader-owned state.
// Call only after the reader and its metadata/cache file owners are destroyed.
bool clearReaderCache(const std::string& cachePath);
class BookMove {
 public:
  BookMove(const std::string& oldCache, const std::string& newCache, const std::string& oldBookPath);
  ~BookMove();
  BookMove(const BookMove&) = delete;
  BookMove& operator=(const BookMove&) = delete;
  bool prepare();
  void commit() { moved = false; }

 private:
  bool prepareLocked();
  StatsSessionLease lease;
  std::string oldCache;
  std::string oldBookPath;
  std::string source;
  std::string destination;
  bool moved = false;
};
bool saveBook(const std::string& directory, const BookReadingStats& stats);
// Reload and edit a closed book under the storage lock so other statistics
// are not replaced by a stale screen snapshot. Unknown or missing data fails.
bool editClosedBookDate(const std::string& cachePath, const std::string& bookPath, bool started,
                        const ReadingStatsDate& date, BookReadingStats& updated);
}  // namespace readingstats
