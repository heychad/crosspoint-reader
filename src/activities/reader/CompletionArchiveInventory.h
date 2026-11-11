#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

class HalStorage;

namespace readingstats::completionjournal {
constexpr size_t MAX_ARCHIVE_INDEX = 1024;
using ArchiveInventory = std::array<uint8_t, MAX_ARCHIVE_INDEX / 8>;
enum class ArchiveInventoryResult { Ready, Conflict, Unavailable };

// A complete root scan is required. Output is unchanged on failure. Archive
// indices outside the supported range block the scan rather than being omitted.
ArchiveInventoryResult inventoryArchives(HalStorage& storage, ArchiveInventory& output);
inline bool hasArchive(const ArchiveInventory& inventory, size_t index) {
  return index < MAX_ARCHIVE_INDEX && (inventory[index / 8] & (uint8_t{1} << (index % 8)));
}
}  // namespace readingstats::completionjournal
