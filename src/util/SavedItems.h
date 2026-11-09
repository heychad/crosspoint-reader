#pragma once

#include <cstdint>
#include <string>

enum class SavedItemKind : uint8_t { Bookmarks, CrossInkBookmarks, Clippings };

namespace saveditems {
constexpr uint8_t BOOKMARKS = 1;
constexpr uint8_t CROSSINK_BOOKMARKS = 2;
constexpr uint8_t HAS_CLIPPINGS = 4;
// False reports unreadable metadata. The caller keeps the failure visible.
bool inspect(const std::string& bookPath, uint8_t& kinds);
}  // namespace saveditems
