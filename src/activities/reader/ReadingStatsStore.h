#pragma once

#include <string>

#include "BookReadingStats.h"

namespace readingstats {
// Separate from rebuildable page caches; the filename still uses the existing book identity.
std::string bookDirectory(const std::string& cachePath);
BookReadingStats loadBook(const std::string& cachePath, const std::string& bookPath, bool& enabled,
                          bool* found = nullptr, BookReadingStats::LoadMode mode = BookReadingStats::LoadMode::Repair,
                          bool* trackingKnown = nullptr);
bool preserveCache(const std::string& cachePath);
// Clear rebuildable data even when statistics cannot be migrated.
bool clearCache(const std::string& cachePath);
class BookMove {
 public:
  BookMove(const std::string& oldCache, const std::string& newCache, const std::string& oldBookPath);
  ~BookMove();
  BookMove(const BookMove&) = delete;
  BookMove& operator=(const BookMove&) = delete;
  bool prepare();
  void commit() { moved = false; }

 private:
  std::string oldCache;
  std::string oldBookPath;
  std::string source;
  std::string destination;
  bool moved = false;
};
bool saveBook(const std::string& directory, const BookReadingStats& stats);
}  // namespace readingstats
