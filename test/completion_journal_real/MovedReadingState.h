#pragma once

#include <ClippingStore.h>
#include <CompletionJournal.h>
#include <CredentialIntegrity.h>
#include <MovedProgressLoader.h>
#include <Serialization.h>
#include <clippings/ClippingPreview.h>
#include <util/BookmarkFile.h>
#include <util/CrossInkBookmarks.h>
#include <util/SavedItems.h>

#include <array>
#include <cassert>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace movedstate {
enum class ProgressFormat { Legacy, PageCount, TextOffset };

inline constexpr char CLIPPING_TEXT[] = "Café — a saved passage.\nSecond paragraph, with <tags> & symbols.";
inline constexpr uint8_t KEY_BYTES[] = {0x46, 0x49, 0x42, 0x4b, 0, 0xff, 0x7f, 0x31};
inline constexpr char RIGHTS_BYTES[] = "{\"plugin\":\"fixture\",\"loan\":\"unchanged\"}";
struct ClippingSnapshot {
  Clipping entry;
  std::string deletionId;
};

inline void seedSidecars(const std::string& book) {
  for (bool key : {true, false}) {
    const auto path = book + (key ? ".key" : ".rights");
    auto file = Storage.open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY);
    const auto* bytes = key ? KEY_BYTES : reinterpret_cast<const uint8_t*>(RIGHTS_BYTES);
    const size_t size = key ? sizeof(KEY_BYTES) : sizeof(RIGHTS_BYTES) - 1;
    assert(file && file.write(bytes, size) == size && file.sync() && file.close());
  }
}

inline void checkSidecars(const std::string& book) {
  for (bool key : {true, false}) {
    auto file = Storage.open((book + (key ? ".key" : ".rights")).c_str(), O_RDONLY);
    const auto* bytes = key ? KEY_BYTES : reinterpret_cast<const uint8_t*>(RIGHTS_BYTES);
    const size_t size = key ? sizeof(KEY_BYTES) : sizeof(RIGHTS_BYTES) - 1;
    uint8_t actual[64]{};
    assert(file && file.fileSize() == size && file.read(actual, size) == static_cast<int>(size));
    assert(std::memcmp(actual, bytes, size) == 0 && file.close());
  }
}

inline void seedNativeBookmarks(const std::string& book) {
  BookmarkEntry entry;
  entry.xpath = "/body/DocFragment[3]/body/div/p[17]";
  entry.percentage = 0.375f;
  entry.summary = "A useful passage";
  entry.name = "Café note";
  entry.computedSpineIndex = 3;
  entry.computedChapterPageCount = 42;
  entry.computedChapterProgress = 7;
  entry.hasVisibleTextOffset = true;
  entry.visibleTextOffset = 12345;
  auto legacy = entry;
  legacy.name = "Earlier bookmark";
  legacy.hasVisibleTextOffset = false;
  assert(BookmarkFile::save(book, {entry, legacy}));
}

inline void checkNativeBookmarks(const std::string& book) {
  std::vector<BookmarkEntry> entries;
  assert(BookmarkFile::load(book, entries) && entries.size() == 2);
  for (const auto& entry : entries) {
    assert(entry.xpath == "/body/DocFragment[3]/body/div/p[17]" && entry.percentage == 0.375f);
    assert(entry.summary == "A useful passage");
    assert(entry.computedSpineIndex == 3 && entry.computedChapterPageCount == 42 && entry.computedChapterProgress == 7);
  }
  assert(entries[0].name == "Café note" && entries[0].hasVisibleTextOffset && entries[0].visibleTextOffset == 12345);
  assert(entries[1].name == "Earlier bookmark" && !entries[1].hasVisibleTextOffset);
}

inline void checkSavedKinds(const std::string& book) {
  uint8_t kinds = 0;
  assert(saveditems::inspect(book, kinds));
  assert(kinds == (saveditems::BOOKMARKS | saveditems::CROSSINK_BOOKMARKS | saveditems::HAS_CLIPPINGS));
}

inline ClippingSnapshot seedClippings(const std::string& book) {
  ClippingStore store;
  assert(store.loadForBook(book, "Book", "Writer", "epub"));
  assert(store.addClipping(3, 7, 8, 42, 2, 5, 12, "Chapter three", 17, CLIPPING_TEXT, 0x1234, 12345, 12420) ==
         ClippingStore::AddResult::Added);
  assert(store.addClipping(3, 9, 9, 42, 1, 2, 2, "Chapter three", 18, "Remove this", 0x1234, 12500, 12511) ==
         ClippingStore::AddResult::Added);
  assert(store.prepareSync());
  Clipping remote = *store.clippingAt(0);
  remote.syncRevision = 77;
  remote.pendingUpload = false;
  assert(store.applyRemote(remote, CLIPPING_TEXT, false));
  const std::string deletion = store.clippingAt(1)->id;
  assert(!deletion.empty() && store.removeClippingAt(1) && store.clippingCount() == 1);
  ClippingSnapshot result{*store.clippingAt(0), deletion};
  store.unload();
  return result;
}

inline void checkClippings(const std::string& book, const ClippingSnapshot& expected) {
  ClippingStore store;
  assert(store.loadForBook(book, "Book", "Writer", "epub") && store.clippingCount() == 1);
  const auto* entry = store.clippingAt(0);
  assert(entry && entry->spineIndex == 3 && entry->startPage == 7 && entry->endPage == 8 && entry->pageCount == 42);
  assert(entry->startWordIndex == 2 && entry->endWordIndex == 5 && entry->wordCount == 12 &&
         entry->paragraphIndex == 17);
  assert(entry->timestamp == expected.entry.timestamp && entry->layoutSignature == 0x1234);
  assert(entry->startOffset == 12345 && entry->endOffset == 12420);
  assert(entry->syncRevision == 77 && !entry->pendingUpload && std::strcmp(entry->id, expected.entry.id) == 0);
  assert(std::strcmp(entry->chapterTitle, "Chapter three") == 0);
  std::string text;
  assert(store.readClippingText(0, text) && text == CLIPPING_TEXT);
  char preview[clippingPreview::BUFFER_BYTES]{};
  size_t length = 0;
  assert(store.readClippingPreview(0, preview, sizeof(preview), length) && length > 0);
  assert(std::strstr(preview, "Café") != nullptr);
  uint32_t offset = 0;
  char deleted[65]{};
  assert(store.nextDeletion(offset, deleted) && expected.deletionId == deleted);
  assert(store.nextDeletion(offset, deleted) && !deleted[0]);
  assert(store.prepareSync());
  store.unload();
}

inline void editMovedItems(const std::string& book, const ClippingSnapshot& expected) {
  std::vector<BookmarkEntry> bookmarks;
  assert(BookmarkFile::load(book, bookmarks));
  bookmarks[0].name = "Renamed after move";
  assert(BookmarkFile::save(book, bookmarks));
  bookmarks.clear();
  assert(BookmarkFile::load(book, bookmarks) && bookmarks.size() == 2);
  assert(bookmarks[0].name == "Renamed after move" && bookmarks[0].hasVisibleTextOffset &&
         bookmarks[0].visibleTextOffset == 12345 && bookmarks[1].name == "Earlier bookmark");
  ClippingStore store;
  assert(store.loadForBook(book, "Book", "Writer", "epub"));
  assert(store.addClipping(4, 1, 1, 30, 0, 2, 3, "Chapter four", 1, "Added after moving", 0x2345, 20000, 20018) ==
         ClippingStore::AddResult::Added);
  assert(store.prepareSync());
  store.unload();
  assert(store.loadForBook(book, "Book", "Writer", "epub") && store.clippingCount() == 2);
  std::string text;
  assert(store.readClippingText(0, text) && text == CLIPPING_TEXT);
  const auto* first = store.clippingAt(0);
  assert(first && std::strcmp(first->id, expected.entry.id) == 0 && first->syncRevision == 77 && !first->pendingUpload);
  assert(store.readClippingText(1, text) && text == "Added after moving");
  assert(store.clippingAt(1)->pendingUpload && store.clippingAt(1)->id[0]);
  store.unload();
  checkSavedKinds(book);
  checkSidecars(book);
}

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
