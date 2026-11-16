#include "BookMoveStorage.h"

#include <HalStorage.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <string_view>

#include "activities/reader/CompletionLibraryDirty.h"
#include "activities/reader/CompletionMoveFile.h"
#include "activities/reader/CrossInkBookmarkMove.h"

namespace bookmove {
namespace {
using namespace readingstats::completionmove;

std::string extension(const std::string& path) {
  const auto dot = path.find_last_of('.');
  if (dot == path.npos || dot < path.find_last_of('/')) return {};
  auto suffix = path.substr(dot);
  for (char& c : suffix)
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return suffix;
}

std::string parent(const std::string& path) {
  const auto slash = path.find_last_of('/');
  return slash == 0 ? "/" : path.substr(0, slash);
}

bool directory(const std::string& path, bool create) {
  auto status = Storage.lookupPath(path.c_str());
  if (create && status == HalStorage::PathStatus::Missing) {
    if (!Storage.mkdir(path.c_str(), true)) return false;
    status = Storage.lookupPath(path.c_str());
  }
  if (status != HalStorage::PathStatus::Present) return false;
  auto file = Storage.openChecked(path.c_str());
  if (!file) return false;
  const bool dir = file.isDirectory();
  return file.close() && dir;
}

Result capture(const std::string& path, EntryKind kind, EntryIdentity& output) {
  if (kind == EntryKind::Directory) {
    uint32_t cluster = 0;
    const auto status = Storage.identifyMoveDirectory(path.c_str(), cluster);
    switch (status) {
      case HalStorage::DirectoryMoveStatus::Ready:
        output = {EntryKind::Directory, cluster, 0, 0};
        return Result::Moved;
      case HalStorage::DirectoryMoveStatus::Missing:
        output = {};
        return Result::Moved;
      case HalStorage::DirectoryMoveStatus::NotDirectory:
        return Result::Conflict;
      case HalStorage::DirectoryMoveStatus::Unavailable:
        return Result::Unavailable;
    }
    return Result::Unavailable;
  }
  FileIdentity file;
  const auto status = captureFile(path, file);
  if (status == FileResult::Absent) {
    output = {};
    return Result::Moved;
  }
  if (status == FileResult::Conflict) return Result::Conflict;
  if (status != FileResult::Ready) return Result::Unavailable;
  output = {kind, 0, file.size, file.checksum};
  return Result::Moved;
}

bool equal(const EntryIdentity& a, const EntryIdentity& b) {
  return a.kind == b.kind && a.firstCluster == b.firstCluster && a.size == b.size && a.checksum == b.checksum;
}

FileIdentity fileIdentity(const EntryIdentity& entry) {
  return {entry.kind != EntryKind::Missing, entry.size, entry.checksum};
}

bool exact(const char* path, const uint8_t* data, size_t size) {
  auto file = Storage.openChecked(path);
  if (!file) return false;
  bool same = !file.isDirectory() && file.fileSize() == size;
  uint8_t bytes[128];
  size_t offset = 0;
  while (same && offset < size) {
    const size_t count = std::min(sizeof(bytes), size - offset);
    same = file.read(bytes, count) == static_cast<int>(count) && std::memcmp(bytes, data + offset, count) == 0;
    offset += count;
  }
  return file.close() && same;
}

bool publish(const char* path, const uint8_t* bytes, size_t size) {
  if (Storage.lookupPath(path) != HalStorage::PathStatus::Missing) return false;
  auto file = Storage.openChecked(path, O_WRONLY | O_CREAT | O_EXCL);
  if (!file) return false;
  const bool written = file.write(bytes, size) == size;
  const bool synced = file.sync();
  const bool closed = file.close();
  return written && synced && closed && exact(path, bytes, size);
}

void seal(const Work& work, uint8_t* output) {
  put64(output, work.id);
  put64(output + 8, readingstats::completionjournal::checksum(work.bytes.data(), work.size));
}

std::string archivePath(uint64_t id) {
  char name[64];
  snprintf(name, sizeof(name), "%s/done_%016llx", JOURNAL_ROOT, static_cast<unsigned long long>(id));
  return name;
}

std::string retainedBookmark(size_t index) { return std::string(CURRENT) + "/bookmark_" + std::to_string(index); }

Result preflight(Work& work) {
  if (!directory(parent(work.to), false) || !destinationReferencesClear(work)) return Result::Conflict;
  std::string from, to, otherFrom, otherTo;
  for (size_t i = 0; i < ENTRY_COUNT; ++i) {
    if (!itemPaths(work.from, work.to, work.kind, i, from, to)) return Result::Invalid;
    auto result = capture(from, ENTRIES[i].kind, work.identities[i]);
    if (result != Result::Moved) return result;
    if (from == to) {
      // A shared state key cannot be rewritten safely for two book paths.
      if (work.identities[i].kind != EntryKind::Missing) return Result::Conflict;
    } else if (Storage.lookupPath(to.c_str()) != HalStorage::PathStatus::Missing) {
      return Result::Conflict;
    }
    if (ENTRIES[i].kind == EntryKind::LegacyBookmark) {
      if (Storage.lookupPath((to + ".completion-copy").c_str()) != HalStorage::PathStatus::Missing)
        return Result::Conflict;
      if (work.identities[i].kind != EntryKind::Missing) {
        HalFile file = Storage.openChecked(from.c_str());
        if (!file) return Result::Unavailable;
        BookmarkRewrite rewrite;
        const auto status = inspectBookmarkRewrite(file, work.from, work.to, rewrite);
        const bool closed = file.close();
        if (!closed || status == BookmarkRewriteResult::Unavailable) return Result::Unavailable;
        if (status != BookmarkRewriteResult::Ready) return Result::Conflict;
      }
    }
    // Refuse collisions across different state families before any mutation.
    for (size_t j = 0; j < i; ++j) {
      if (!itemPaths(work.from, work.to, work.kind, j, otherFrom, otherTo)) return Result::Invalid;
      if (from == otherTo || to == otherFrom || from == otherFrom || to == otherTo) return Result::Conflict;
    }
  }
  return work.identities[0].kind == EntryKind::File ? Result::Moved : Result::Conflict;
}

Result moveEntry(const Work& work, size_t index) {
  std::string from, to;
  if (!itemPaths(work.from, work.to, work.kind, index, from, to)) return Result::RecoveryRequired;
  const auto& expected = work.identities[index];
  if (from == to) {
    EntryIdentity current;
    const auto captured = capture(from, ENTRIES[index].kind, current);
    return captured == Result::Moved && expected.kind == EntryKind::Missing && equal(current, expected)
               ? Result::Moved
               : Result::Conflict;
  }
  if (!directory(parent(to), true)) return Result::RecoveryRequired;
  if (ENTRIES[index].kind == EntryKind::LegacyBookmark && expected.kind != EntryKind::Missing) {
    const auto retained = retainedBookmark(index);
    EntryIdentity original, saved;
    const auto a = capture(from, EntryKind::LegacyBookmark, original);
    const auto b = capture(retained, EntryKind::LegacyBookmark, saved);
    if (a != Result::Moved || b != Result::Moved) return Result::RecoveryRequired;
    if (equal(original, expected) && saved.kind == EntryKind::Missing) {
      const auto copied = copyBookmarkChecked(from, to, work.from, work.to, fileIdentity(expected));
      if (copied != FileResult::Copied && copied != FileResult::AlreadyCopied) return Result::RecoveryRequired;
      const auto archived = moveFileChecked(from, retained, fileIdentity(expected));
      if (archived != FileResult::Moved && archived != FileResult::AlreadyMoved) return Result::RecoveryRequired;
    } else if (original.kind != EntryKind::Missing || !equal(saved, expected)) {
      return Result::Conflict;
    }
    const auto verified = copyBookmarkChecked(retained, to, work.from, work.to, fileIdentity(expected));
    return verified == FileResult::AlreadyCopied ? Result::Moved : Result::RecoveryRequired;
  }
  if (ENTRIES[index].kind == EntryKind::Directory) {
    EntryIdentity source, target;
    if (capture(from, EntryKind::Directory, source) != Result::Moved ||
        capture(to, EntryKind::Directory, target) != Result::Moved)
      return Result::RecoveryRequired;
    if (equal(source, expected) && target.kind == EntryKind::Missing) {
      if (expected.kind == EntryKind::Missing) return Result::Moved;
      if (!Storage.rename(from.c_str(), to.c_str())) return Result::RecoveryRequired;
      if (capture(from, EntryKind::Directory, source) != Result::Moved ||
          capture(to, EntryKind::Directory, target) != Result::Moved)
        return Result::RecoveryRequired;
    }
    return source.kind == EntryKind::Missing && equal(target, expected) ? Result::Moved : Result::Conflict;
  }
  const auto moved = moveFileChecked(from, to, fileIdentity(expected));
  if (moved == FileResult::Moved || moved == FileResult::AlreadyMoved || moved == FileResult::Absent)
    return Result::Moved;
  return moved == FileResult::Conflict ? Result::Conflict : Result::RecoveryRequired;
}

bool invalidateGenerated(const Work& work) {
  constexpr const char* generated[] = {"sections", "css_rules.cache", "css_rules.cache.tmp", "css_rules.cache.bak"};
  for (size_t index : {size_t{21}, size_t{22}}) {
    if (work.identities[index].kind != EntryKind::Directory) continue;
    std::string from, cache;
    if (!itemPaths(work.from, work.to, work.kind, index, from, cache)) return false;
    for (const char* leaf : generated) {
      const auto path = cache + "/" + leaf;
      const auto state = Storage.lookupPath(path.c_str());
      if (state == HalStorage::PathStatus::Missing) continue;
      if (state != HalStorage::PathStatus::Present) return false;
      // Only these generated files depend on the old absolute cache path.
      const bool removed =
          std::strcmp(leaf, "sections") == 0 ? Storage.removeDir(path.c_str()) : Storage.remove(path.c_str());
      if (!removed || Storage.lookupPath(path.c_str()) != HalStorage::PathStatus::Missing) return false;
    }
  }
  return true;
}
}  // namespace

bool managedBook(const std::string& path) {
  const auto ext = extension(path);
  return ext == ".epub" || ext == ".txt" || ext == ".md" || ext == ".xtc" || ext == ".xtch";
}

bool bookKind(const std::string& from, const std::string& to, BookKind& kind) {
  if (!managedBook(from) || extension(from) != extension(to)) return false;
  const auto ext = extension(from);
  kind = ext == ".xtc" || ext == ".xtch" ? BookKind::Xtc : BookKind::Reflowable;
  return true;
}

bool pending() { return Storage.lookupPath(CURRENT) != HalStorage::PathStatus::Missing; }

Result prepare(Work& work) {
  if (!validMovePath(work.from) || !validMovePath(work.to) || work.from == work.to ||
      !bookKind(work.from, work.to, work.kind))
    return Result::Invalid;
  if (pending()) return Result::RecoveryRequired;
  const auto ready = preflight(work);
  if (ready != Result::Moved) return ready;
  work.id =
      readingstats::completionjournal::checksum(reinterpret_cast<const uint8_t*>(work.from.data()), work.from.size());
  bool available = false;
  for (unsigned attempt = 0; attempt < 128; ++attempt) {
    if (++work.id == 0) ++work.id;
    work.archive = archivePath(work.id);
    if (Storage.lookupPath(work.archive.c_str()) == HalStorage::PathStatus::Missing) {
      available = true;
      break;
    }
  }
  if (!available || !encodePlan(work.from, work.to, work.id, work.identities, work.bytes, work.size))
    return Result::Unavailable;
  if (!directory(JOURNAL_ROOT, true) || !directory(CURRENT, true)) return Result::RecoveryRequired;
  if (!publish(PLAN, work.bytes.data(), work.size)) return Result::RecoveryRequired;
  uint8_t proof[16];
  seal(work, proof);
  return publish(SEAL, proof, sizeof(proof)) ? Result::Moved : Result::RecoveryRequired;
}

Result inspect(Work& work) {
  if (!pending()) return Result::NothingPending;
  auto file = Storage.openChecked(PLAN);
  if (!file) return Result::RecoveryRequired;
  const size_t size = file.fileSize();
  const bool read =
      !file.isDirectory() && size <= work.bytes.size() && file.read(work.bytes.data(), size) == static_cast<int>(size);
  const bool closed = file.close();
  std::string_view from, to;
  if (!read || !closed || !decodePlan(work.bytes.data(), size, from, to, work.id, &work.identities))
    return Result::RecoveryRequired;
  work.size = size;
  work.from = from;
  work.to = to;
  work.archive = archivePath(work.id);
  if (!bookKind(work.from, work.to, work.kind)) return Result::RecoveryRequired;
  uint8_t proof[16];
  seal(work, proof);
  return exact(SEAL, proof, sizeof(proof)) ? Result::Moved : Result::RecoveryRequired;
}

Result execute(Work& work) {
  // The immutable, separately sealed plan must be published before any move.
  uint8_t proof[16];
  seal(work, proof);
  if (!exact(PLAN, work.bytes.data(), work.size) || !exact(SEAL, proof, sizeof(proof)) ||
      Storage.lookupPath(work.archive.c_str()) != HalStorage::PathStatus::Missing)
    return Result::RecoveryRequired;
  if (!readingstats::completionmove::markLibraryDirtyForMove()) return Result::RecoveryRequired;
  // Preserve book bytes through rename; move all state before publishing its new path.
  for (size_t i = 1; i < ENTRY_COUNT; ++i) {
    const auto result = moveEntry(work, i);
    if (result != Result::Moved) return result;
  }
  const auto moved = moveEntry(work, 0);
  if (moved != Result::Moved) return moved;
  if (!invalidateGenerated(work) || !updateReferences(work)) return Result::RecoveryRequired;
  // Keep the plan, seal, and original legacy bookmarks in a unique archive.
  if (!Storage.rename(CURRENT, work.archive.c_str())) return Result::RecoveryRequired;
  return Storage.lookupPath(CURRENT) == HalStorage::PathStatus::Missing && directory(work.archive, false)
             ? Result::Moved
             : Result::RecoveryRequired;
}
}  // namespace bookmove
