#include "SavedItems.h"

#include <HalStorage.h>
#include <JsonListParser.h>
#include <Logging.h>
#include <Memory.h>

#include "BookmarkUtil.h"
#include "ClippingStore.h"
#include "CrossInkBookmarks.h"

namespace {
// Keep one catalog batch responsive even for very large legacy bookmark files.
constexpr uint16_t MAX_LEGACY_RECORDS_PER_BOOK = 8;
bool hasBookmarks(const std::string& path, bool& present) {
  present = false;
  if (!Storage.exists(path.c_str())) return true;
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
  return parser->parse(
      [](void* ctx, char* buffer, size_t length) {
        auto& input = *static_cast<HalFile*>(ctx);
        if (!input.available()) return 0;
        const int bytes = input.read(buffer, length);
        return bytes > 0 ? bytes : -1;
      },
      &file);
}
}  // namespace

bool saveditems::inspect(const std::string& bookPath, uint8_t& kinds) {
  kinds = 0;
  bool bookmarks = false;
  const bool readable = hasBookmarks(BookmarkUtil::getBookmarkPath(bookPath), bookmarks);
  if (bookmarks) kinds |= BOOKMARKS;
  CrossInkBookmarks legacy;
  if (legacy.open(bookPath) && legacy.count() > 0) kinds |= CROSSINK_BOOKMARKS;
  bool legacyReadable = !legacy.unreadable() && legacy.count() <= MAX_LEGACY_RECORDS_PER_BOOK;
  CrossInkBookmarks::Entry legacyEntry;
  for (uint16_t i = 0; i < legacy.count() && i < MAX_LEGACY_RECORDS_PER_BOOK; ++i) {
    if (!legacy.read(i, legacyEntry)) legacyReadable = false;
  }
  uint16_t clippings = 0;
  const bool clippingReadable = ClippingStore::countForBook(bookPath, clippings);
  if (clippings) kinds |= HAS_CLIPPINGS;
  if (!readable || !clippingReadable || !legacyReadable) {
    LOG_ERR("SAVED", "Unreadable saved items for %s", bookPath.c_str());
    return false;
  }
  return true;
}
