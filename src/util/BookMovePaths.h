#pragma once

#include <CredentialIntegrity.h>

#include <array>
#include <functional>
#include <string>
#include <string_view>

#include "BookMovePlan.h"
#include "BookmarkUtil.h"

namespace bookmove {
enum class BookKind : uint8_t { Reflowable, Xtc };
enum class Root : uint8_t {
  Book,
  Clippings,
  NativeBookmarks,
  CrcBookmarks,
  HashBookmarks,
  Cache,
  LegacyCache,
  Statistics,
  LegacyStatistics,
  Backups,
  LegacyBackups
};
struct EntrySpec {
  Root root;
  const char* suffix;
  EntryKind kind;
};
inline constexpr std::array<EntrySpec, ENTRY_COUNT> ENTRIES{{
    {Root::Book, "", EntryKind::File},
    {Root::Book, ".key", EntryKind::File},
    {Root::Book, ".rights", EntryKind::File},
    {Root::Clippings, "", EntryKind::File},
    {Root::Clippings, ".bak", EntryKind::File},
    {Root::Clippings, ".tmp", EntryKind::File},
    {Root::Clippings, ".deleted", EntryKind::File},
    {Root::Clippings, ".migrate.bak", EntryKind::File},
    {Root::NativeBookmarks, "", EntryKind::File},
    {Root::NativeBookmarks, ".tmp", EntryKind::File},
    {Root::NativeBookmarks, ".bak", EntryKind::File},
    {Root::CrcBookmarks, "", EntryKind::LegacyBookmark},
    {Root::CrcBookmarks, ".tmp", EntryKind::LegacyBookmark},
    {Root::CrcBookmarks, ".bak", EntryKind::LegacyBookmark},
    {Root::CrcBookmarks, ".bookmark-edit.bak", EntryKind::LegacyBookmark},
    {Root::CrcBookmarks, ".bookmark-edit.guard", EntryKind::LegacyBookmark},
    {Root::HashBookmarks, "", EntryKind::LegacyBookmark},
    {Root::HashBookmarks, ".tmp", EntryKind::LegacyBookmark},
    {Root::HashBookmarks, ".bak", EntryKind::LegacyBookmark},
    {Root::HashBookmarks, ".bookmark-edit.bak", EntryKind::LegacyBookmark},
    {Root::HashBookmarks, ".bookmark-edit.guard", EntryKind::LegacyBookmark},
    {Root::Cache, "", EntryKind::Directory},
    {Root::LegacyCache, "", EntryKind::Directory},
    {Root::Statistics, "", EntryKind::Directory},
    {Root::LegacyStatistics, "", EntryKind::Directory},
    {Root::Backups, "", EntryKind::Directory},
    {Root::LegacyBackups, "", EntryKind::Directory},
    {Root::Book, ".key.tmp", EntryKind::File},
    {Root::Book, ".key.bak", EntryKind::File},
    {Root::Book, ".rights.tmp", EntryKind::File},
    {Root::Book, ".rights.bak", EntryKind::File},
}};
static_assert([] {
  for (size_t i = 0; i < ENTRIES.size(); ++i)
    if (ENTRIES[i].kind != expectedKind(i)) return false;
  return true;
}());

inline std::string cacheLeaf(std::string_view book, BookKind kind, bool legacy = false) {
  const auto hash =
      legacy ? readingstats::completionjournal::checksum(reinterpret_cast<const uint8_t*>(book.data()), book.size())
             : static_cast<uint64_t>(std::hash<std::string>{}(std::string(book)));
  return std::string(kind == BookKind::Reflowable ? "epub_" : "xtc_") + std::to_string(hash);
}

inline std::string rootPath(std::string_view book, BookKind kind, Root root) {
  switch (root) {
    case Root::Book:
      return std::string(book);
    case Root::Clippings:
      return "/.crosspoint/clippings/" + cacheLeaf(book, BookKind::Reflowable) + ".bin";
    case Root::NativeBookmarks:
      return BookmarkUtil::getBookmarkPath(std::string(book));
    case Root::CrcBookmarks:
      return "/.crosspoint/bookmarks/epub_" + std::to_string(credential_integrity::crc32(book)) + ".bin";
    case Root::HashBookmarks:
      return "/.crosspoint/bookmarks/" + cacheLeaf(book, BookKind::Reflowable) + ".bin";
    case Root::Cache:
      return "/.crosspoint/" + cacheLeaf(book, kind);
    case Root::LegacyCache:
      return "/.crosspoint/" + cacheLeaf(book, kind, true);
    case Root::Statistics:
      return "/.crosspoint/reading_stats/" + cacheLeaf(book, kind);
    case Root::LegacyStatistics:
      return "/.crosspoint/reading_stats/" + cacheLeaf(book, kind, true);
    case Root::Backups:
      return "/.crossink-stats-backup/books/" + cacheLeaf(book, kind);
    case Root::LegacyBackups:
      return "/.crossink-stats-backup/books/" + cacheLeaf(book, kind, true);
  }
  return {};
}

inline bool itemPaths(std::string_view from, std::string_view to, BookKind kind, size_t index, std::string& source,
                      std::string& target) {
  if (!validMovePath(from) || !validMovePath(to) || index >= ENTRIES.size()) return false;
  const auto& entry = ENTRIES[index];
  source = rootPath(from, kind, entry.root) + entry.suffix;
  target = rootPath(to, kind, entry.root) + entry.suffix;
  return true;
}
}  // namespace bookmove
