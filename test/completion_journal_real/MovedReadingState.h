#pragma once

#include <CompletionJournal.h>
#include <CredentialIntegrity.h>
#include <MovedProgressLoader.h>
#include <Serialization.h>
#include <util/CrossInkBookmarks.h>

#include <array>
#include <cassert>
#include <cstring>
#include <functional>
#include <string>

namespace movedstate {
enum class ProgressFormat { Legacy, PageCount, TextOffset };

inline std::string cachePath(const std::string& book, bool legacy = false) {
  const uint64_t key =
      legacy ? readingstats::completionjournal::checksum(reinterpret_cast<const uint8_t*>(book.data()), book.size())
             : std::hash<std::string>{}(book);
  return "/.crosspoint/epub_" + std::to_string(key);
}

inline void seedProgress(const std::string& book, bool legacy, ProgressFormat format) {
  const auto cache = cachePath(book, legacy);
  assert(Storage.mkdir(cache.c_str(), true));
  auto file = Storage.open((cache + "/progress.bin").c_str(), O_CREAT | O_EXCL | O_WRONLY);
  assert(file && serialization::tryWritePod(file, uint16_t{3}) && serialization::tryWritePod(file, uint16_t{7}));
  switch (format) {
    case ProgressFormat::Legacy:
      break;
    case ProgressFormat::PageCount:
      assert(serialization::tryWritePod(file, uint16_t{42}));
      break;
    case ProgressFormat::TextOffset:
      assert(serialization::tryWritePod(file, uint16_t{42}) && serialization::tryWritePod(file, uint32_t{12345}));
      break;
  }
  assert(file.sync() && file.close());
}

inline void checkProgress(const std::string& book, ProgressFormat format) {
  const auto position = readMovedProgress(cachePath(book));
  assert(position.spine == 3 && position.cachedSpine == 3 && position.page == 7);
  assert(position.pages == (format == ProgressFormat::Legacy ? 0 : 42));
  const bool hasOffset = format == ProgressFormat::TextOffset;
  assert(position.offset.has_value() == hasOffset);
  if (hasOffset) assert(*position.offset == 12345U);
}

inline std::string bookmarkPath(const std::string& book, bool crc) {
  const size_t key = crc ? credential_integrity::crc32(book) : std::hash<std::string>{}(book);
  return "/.crosspoint/bookmarks/epub_" + std::to_string(key) + ".bin";
}

inline void seedBookmarks(const std::string& book, uint8_t firstVersion) {
  assert(Storage.mkdir("/.crosspoint/bookmarks", true));
  for (unsigned index = 0; index < 2; ++index) {
    const auto path = bookmarkPath(book, index == 0);
    auto file = Storage.open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY);
    assert(file);
    const auto version = static_cast<uint8_t>(firstVersion + index);
    assert(version >= 2 && version <= 5);
    assert(serialization::tryWritePod(file, version));
    if (version == 2)
      assert(serialization::tryWritePod(file, uint8_t{1}));
    else
      assert(serialization::tryWritePod(file, uint16_t{1}));
    assert(serialization::tryWriteString(file, "Book") && serialization::tryWriteString(file, "Writer") &&
           serialization::tryWriteString(file, book));
    assert(serialization::tryWritePod(file, uint16_t{3}) && serialization::tryWritePod(file, float{0.375}) &&
           serialization::tryWritePod(file, uint32_t{1700000000}));
    char chapter[48] = "Chapter three";
    assert(file.write(chapter, sizeof(chapter)) == sizeof(chapter));
    if (version >= 4) assert(serialization::tryWritePod(file, uint16_t{17}));
    if (version >= 5) {
      char snippet[64] = "Remember this passage";
      assert(file.write(snippet, sizeof(snippet)) == sizeof(snippet));
    }
    assert(file.sync() && file.close());
  }
}

inline void checkBookmarks(const std::string& book, uint8_t firstVersion, bool firstRecovered = false) {
  CrossInkBookmarks reader;
  assert(reader.open(book) && reader.count() == 2 && reader.unreadable() == firstRecovered);
  for (uint16_t index = 0; index < 2; ++index) {
    CrossInkBookmarks::Entry entry;
    assert(reader.read(index, entry));
    assert(entry.spine == 3 && entry.progress == float{0.375});
    assert(std::strcmp(entry.chapter, "Chapter three") == 0);
    const auto version = firstVersion + index;
    assert(entry.paragraph == (version >= 4 ? 17 : UINT16_MAX));
    assert(std::strcmp(entry.snippet, version >= 5 ? "Remember this passage" : "") == 0);
    assert(entry.recovered == (firstRecovered && index == 0));
  }
  reader.close();
}
}  // namespace movedstate
