#pragma once
#include <string>
#include <vector>

#include "../BookmarkEntry.h"

// Per-book bookmark persistence. Takes the book's path; bookmark-file path
// derivation (BookmarkUtil) and directory creation are hidden inside.
namespace BookmarkFile {

enum class LoadResult { Loaded, Missing, Unavailable };
// A confirmed missing file clears the vector. An unreadable file leaves it intact.
LoadResult load(const std::string& bookPath, std::vector<BookmarkEntry>& bookmarks);

// Saves the bookmarks for bookPath, creating the bookmarks directory as needed.
struct Omit {
  bool (*matches)(const BookmarkEntry&, const void*) = nullptr;
  const void* context = nullptr;
};
bool save(const std::string& bookPath, const std::vector<BookmarkEntry>& bookmarks, Omit omit = {});
// Clear native and verified imported bookmarks. Failure can leave some files cleared.
bool clearForBook(const std::string& bookPath);

}  // namespace BookmarkFile
