#include "SavedItems.h"

#include <HalStorage.h>
#include <JsonListParser.h>
#include <Logging.h>
#include <Memory.h>

#include "BookmarkReadPath.h"
#include "ClippingStore.h"
#include "CrossInkBookmarks.h"

namespace {
// Keep one catalog batch responsive even for very large legacy bookmark files.
constexpr uint16_t MAX_LEGACY_RECORDS_PER_BOOK = 8;
struct NativeScan {
  std::string path;
  bool present = false;
};

bool scanNativeBookmarks(void* raw) {
  auto& [path, present] = *static_cast<NativeScan*>(raw);
  const auto status = selectNativeBookmarkFile(path);
  if (status == HalStorage::PathStatus::Missing) return true;
  if (status != HalStorage::PathStatus::Present) return false;
  HalFile file = Storage.open(path.c_str());
  if (!file) return false;
  const std::string empty;
  const std::string* fields[JsonListParser::F_COUNT] = {&empty, &empty, &empty, &empty, &empty, &empty};
  auto parser = makeUniqueNoThrow<JsonListParser>(
      "bookmarks", fields, empty, [](void* ctx, JsonListParser::Row&) { *static_cast<bool*>(ctx) = true; }, &present);
  if (!parser) {
    LOG_ERR("SAVED", "OOM: bookmark scanner (%zu bytes)", sizeof(JsonListParser));
    return false;
  }
  parser->setCaptureBudget(0);
  const bool parsed = parser->parse(
      [](void* ctx, char* buffer, size_t length) {
        auto& input = *static_cast<HalFile*>(ctx);
        if (!input.available()) return 0;
        const int bytes = input.read(buffer, length);
        return bytes > 0 ? bytes : -1;
      },
      &file);
  return file.close() && parsed;
}
}  // namespace

bool saveditems::inspect(const std::string& bookPath, uint8_t& kinds, const uint8_t candidates) {
  kinds = 0;
  bool readable = true;
  if (candidates & BOOKMARKS) {
    NativeScan native{BookmarkUtil::getBookmarkPath(bookPath)};
    readable = Storage.withStorageLock(scanNativeBookmarks, &native);
    if (native.present) kinds |= BOOKMARKS;
  }
  CrossInkBookmarks legacy;
  if ((candidates & CROSSINK_BOOKMARKS) && legacy.open(bookPath) && legacy.count() > 0) kinds |= CROSSINK_BOOKMARKS;
  bool legacyReadable = !legacy.unreadable() && legacy.count() <= MAX_LEGACY_RECORDS_PER_BOOK;
  CrossInkBookmarks::Entry legacyEntry;
  for (uint16_t i = 0; i < legacy.count() && i < MAX_LEGACY_RECORDS_PER_BOOK; ++i) {
    if (!legacy.read(i, legacyEntry)) legacyReadable = false;
  }
  uint16_t clippings = 0;
  const bool clippingReadable = !(candidates & HAS_CLIPPINGS) || ClippingStore::countForBook(bookPath, clippings);
  if (clippings) kinds |= HAS_CLIPPINGS;
  if (!readable || !clippingReadable || !legacyReadable) {
    LOG_ERR("SAVED", "Unreadable saved items for %s", bookPath.c_str());
    return false;
  }
  return true;
}
