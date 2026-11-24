#include "SavedItemsCandidates.h"

#include <CheckedStoragePath.h>
#include <Memory.h>

#include "BookMovePaths.h"
#include "SavedItems.h"

namespace saveditems {
namespace {
constexpr const char* DIRECTORIES[] = {"/.crosspoint/bookmarks", "/.crosspoint/clippings"};
constexpr unsigned RECORDS_PER_BATCH = 32;

uint32_t foldedHash(std::string_view name) {
  uint32_t hash = 2166136261U;
  for (const unsigned char c : name) {
    const unsigned char folded = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
    hash = (hash ^ folded) * 16777619U;
  }
  return hash;
}
}  // namespace

void Candidates::Names::insert(std::string_view name) {
  any = true;
  const auto units = checkedStorageComponentUnits(name);
  if (!units) {
    reliable = false;
    return;
  }
  lengths[units] = true;
  if (units != name.size()) {
    nonAsciiLengths[units] = true;
    return;
  }
  const uint32_t hash = foldedHash(name);
  for (const uint32_t bit : {hash % BIT_COUNT, (hash >> 16) % BIT_COUNT}) {
    bits[bit / 32] |= 1UL << (bit % 32);
  }
}

bool Candidates::Names::mightContain(std::string_view name) const {
  if (!reliable) return true;
  if (!any) return false;
  name.remove_prefix(name.find_last_of('/') + 1);
  const auto units = checkedStorageComponentUnits(name);
  if (!units) return true;
  // A FAT short alias can have a different length from its long name. Send any
  // possible 8.3 lookup to the filesystem, including multibyte OEM names.
  if (units <= 12) return true;
  // SdFat compares long names one UTF-16 unit at a time on FAT and exFAT.
  if (!lengths[units]) return false;
  if (nonAsciiLengths[units] || units != name.size()) return true;
  const uint32_t hash = foldedHash(name);
  for (const uint32_t bit : {hash % BIT_COUNT, (hash >> 16) % BIT_COUNT}) {
    if ((bits[bit / 32] & (1UL << (bit % 32))) == 0) return false;
  }
  return true;
}

bool Candidates::Names::mightContainAny(std::string& path, std::initializer_list<const char*> suffixes) const {
  if (!reliable || !any) return !reliable;
  if (mightContain(path)) return true;
  const auto length = path.size();
  for (const auto* suffix : suffixes) {
    path.append(suffix);
    const bool match = mightContain(path);
    path.resize(length);
    if (match) return true;
  }
  return false;
}

Candidates::Candidates() : initialized(Storage.beginMutationSnapshot(snapshot)) {}

bool Candidates::valid() const { return initialized && Storage.mutationSnapshotUnchanged(snapshot); }

bool Candidates::empty() const {
  return current == STORE_COUNT && stores[0].reliable && stores[1].reliable && !stores[0].any && !stores[1].any;
}

bool Candidates::addEntry(void* context, const wirecheck::DirectoryEntry& entry) {
  static_cast<Names*>(context)->insert(entry.name);
  return true;
}

bool Candidates::advance() {
  if (!valid() || current == STORE_COUNT) return true;
  auto& names = stores[current];
  if (!scan) {
    const auto status = Storage.lookupPath(DIRECTORIES[current]);
    if (status == HalStorage::PathStatus::Present) {
      auto directory = Storage.openChecked(DIRECTORIES[current]);
      if (directory && directory.isDirectory()) {
        scan = makeUniqueNoThrow<wirecheck::StorageDirectoryScan>(
            Storage, snapshot, std::move(directory),
            wirecheck::StorageDirectoryScan::Callbacks{&names, [](void*) { return true; }, addEntry});
      }
    }
    if (status != HalStorage::PathStatus::Missing && !scan) names.reliable = false;
  }
  if (scan) {
    for (unsigned i = 0; i < RECORDS_PER_BATCH; ++i) {
      const auto result = scan->step();
      if (result == wirecheck::ScanResult::Pending) continue;
      scan.reset();
      names.reliable &= result == wirecheck::ScanResult::Complete;
      break;
    }
    if (scan) return false;
  }
  return ++current == STORE_COUNT;
}

uint8_t Candidates::kindsFor(std::string_view book) const {
  using bookmove::BookKind;
  using bookmove::Root;
  uint8_t kinds = 0;
  const auto path = [book](Root root) { return bookmove::rootPath(book, BookKind::Reflowable, root); };
  const auto& bookmarks = stores[0];
  // Native clippings and hash-based legacy bookmarks use the same basename.
  auto hash = path(Root::HashBookmarks);
  if (stores[1].mightContainAny(hash, {".bak"})) kinds |= HAS_CLIPPINGS;
  if (!bookmarks.reliable) return kinds | BOOKMARKS | CROSSINK_BOOKMARKS;
  if (!bookmarks.any) return kinds;
  const auto native = path(Root::NativeBookmarks);
  if (bookmarks.mightContain(native) || bookmarks.mightContain(BookmarkUtil::nativeRecoveryPath(native, true)) ||
      bookmarks.mightContain(BookmarkUtil::nativeRecoveryPath(native, false)))
    kinds |= BOOKMARKS;
  const auto edits = {".bookmark-edit.bak", ".bookmark-edit.guard"};
  if (bookmarks.mightContainAny(hash, edits)) return kinds | CROSSINK_BOOKMARKS;
  auto crc = path(Root::CrcBookmarks);
  if (bookmarks.mightContainAny(crc, edits)) kinds |= CROSSINK_BOOKMARKS;
  return kinds;
}

static_assert(sizeof(Candidates) + sizeof(wirecheck::StorageDirectoryScan) <= 3072);
}  // namespace saveditems
