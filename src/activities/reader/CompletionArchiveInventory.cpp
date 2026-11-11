#include "CompletionArchiveInventory.h"

#include <HalStorage.h>
#include <Memory.h>
#include <network/sync/PathDirectoryScan.h>
#include <strings.h>

#include <cstring>
#include <string_view>

namespace readingstats::completionjournal {
namespace {
constexpr char PREFIX[] = ".completion-history-";
constexpr size_t PREFIX_LENGTH = sizeof(PREFIX) - 1;
constexpr size_t MAX_ROOT_ENTRIES = 16384;

struct ScanContext {
  ArchiveInventory inventory{};
  size_t entries = 0;
  bool conflict = false;
  bool unavailable = false;
};

bool keepGoing(void*) { return true; }

bool addEntry(void* raw, const wirecheck::DirectoryEntry& entry) {
  auto& context = *static_cast<ScanContext*>(raw);
  if (++context.entries > MAX_ROOT_ENTRIES) {
    context.unavailable = true;
    return false;
  }
  const std::string_view name(entry.name);
  if (name.size() < PREFIX_LENGTH || strncasecmp(name.data(), PREFIX, PREFIX_LENGTH) != 0) return true;
  if (name.size() != PREFIX_LENGTH + 6 || name.substr(0, PREFIX_LENGTH) != PREFIX || !entry.directory) {
    context.conflict = true;
    return false;
  }
  size_t index = 0;
  for (const char digit : name.substr(PREFIX_LENGTH)) {
    if (digit < '0' || digit > '9') {
      context.conflict = true;
      return false;
    }
    index = index * 10 + static_cast<unsigned>(digit - '0');
  }
  if (index >= MAX_ARCHIVE_INDEX) {
    context.unavailable = true;
    return false;
  }
  const auto bit = static_cast<uint8_t>(uint8_t{1} << (index % 8));
  if (context.inventory[index / 8] & bit) {
    context.conflict = true;
    return false;
  }
  context.inventory[index / 8] |= bit;
  return true;
}
}  // namespace

ArchiveInventoryResult inventoryArchives(HalStorage& storage, ArchiveInventory& output) {
  HalStorage::MutationSnapshot snapshot;
  if (!storage.beginMutationSnapshot(snapshot)) return ArchiveInventoryResult::Unavailable;
  ScanContext context;
  auto scanner = makeUniqueNoThrow<wirecheck::PathDirectoryScan>(
      storage, snapshot, "/", wirecheck::PathDirectoryScan::Callbacks{&context, keepGoing, addEntry});
  if (!scanner) return ArchiveInventoryResult::Unavailable;
  auto status = wirecheck::ScanResult::Pending;
  while (status == wirecheck::ScanResult::Pending) status = scanner->step();
  if (context.conflict) return ArchiveInventoryResult::Conflict;
  if (context.unavailable || status != wirecheck::ScanResult::Complete || !storage.mutationSnapshotUnchanged(snapshot))
    return ArchiveInventoryResult::Unavailable;
  output = context.inventory;
  return ArchiveInventoryResult::Ready;
}
}  // namespace readingstats::completionjournal
