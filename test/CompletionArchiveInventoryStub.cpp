// Host test adapter. Production inventory is exercised by the real FAT/exFAT scan fixture.
#include <HalStorage.h>

#include <cstdio>

#include "CompletionArchiveInventory.h"

namespace readingstats::completionjournal {
ArchiveInventoryResult inventoryArchives(HalStorage& storage, ArchiveInventory& output) {
  ArchiveInventory candidate{};
  // Journal fixtures use indices 0 through 7. Production scans the entire root.
  for (unsigned index = 0; index < 8; ++index) {
    char path[48];
    std::snprintf(path, sizeof(path), "/.completion-history-%06u", index);
    const auto status = storage.lookupPath(path);
    if (status == HalStorage::PathStatus::Failed) return ArchiveInventoryResult::Unavailable;
    if (status == HalStorage::PathStatus::Present)
      candidate[index / 8] |= static_cast<uint8_t>(uint8_t{1} << (index % 8));
  }
  output = candidate;
  return ArchiveInventoryResult::Ready;
}
}  // namespace readingstats::completionjournal
