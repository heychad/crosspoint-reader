#pragma once

#include <cstdint>
#include <string>

enum class SavedItemKind : uint8_t { Bookmarks, CrossInkBookmarks, Clippings };

namespace saveditems {
constexpr uint8_t BOOKMARKS = 1;
constexpr uint8_t CROSSINK_BOOKMARKS = 2;
constexpr uint8_t HAS_CLIPPINGS = 4;
constexpr uint8_t ALL_KINDS = BOOKMARKS | CROSSINK_BOOKMARKS | HAS_CLIPPINGS;
// False reports unreadable metadata. The caller keeps the failure visible.
bool inspect(const std::string& bookPath, uint8_t& kinds, uint8_t candidates = ALL_KINDS);
}  // namespace saveditems
