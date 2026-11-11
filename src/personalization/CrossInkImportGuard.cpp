#include "CrossInkImportGuard.h"

#include <HalStorage.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace crossinkimport {
namespace {
constexpr char DIRECTORY[] = "/.crosspoint/crossink-import";
constexpr char PRIMARY[] = "/.crosspoint/settings.json";
constexpr char PENDING[] = ".cip-";
constexpr char DONE[] = ".cid-";
constexpr char SNAPSHOT[] = "/.crosspoint/settings-crossink-%02u.bak";

uint64_t hash(const uint8_t* data, size_t size) {
  uint64_t value = 14695981039346656037ULL;
  for (size_t i = 0; i < size; ++i) value = (value ^ data[i]) * 1099511628211ULL;
  return value;
}
void put64(ImportMarker& marker, size_t offset, uint64_t value) {
  for (size_t i = 0; i < 8; ++i) marker[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}
bool markerPath(const char* prefix, unsigned slot, char (&path)[64]) {
  const int n = std::snprintf(path, sizeof(path), "%s/%s%02u", DIRECTORY, prefix, slot);
  return n > 0 && static_cast<size_t>(n) < sizeof(path);
}
bool snapshotPath(unsigned slot, char (&path)[64]) {
  const int n = std::snprintf(path, sizeof(path), SNAPSHOT, slot);
  return n > 0 && static_cast<size_t>(n) < sizeof(path);
}
bool valid(const ImportMarker& marker, unsigned slot) {
  if (slot >= 64 || std::memcmp(marker.data(), "CIG1", 4) || marker[4] != slot) return false;
  for (size_t i = 5; i < 8; ++i)
    if (marker[i]) return false;
  ImportMarker expected = marker;
  put64(expected, 24, hash(marker.data(), 24));
  return expected == marker;
}
bool readExact(const char* path, ImportMarker& marker) {
  HalFile file = Storage.openChecked(path, O_RDONLY);
  if (!file) return false;
  const bool ok = !file.isDirectory() && file.fileSize64() == marker.size() &&
                  file.read(marker.data(), marker.size()) == static_cast<int>(marker.size());
  return file.close() && ok;
}
bool writeExclusive(const char* path, const ImportMarker& marker) {
  HalFile file = Storage.openChecked(path, O_WRONLY | O_CREAT | O_EXCL);
  if (!file) return false;
  const bool written = file.write(marker.data(), marker.size()) == marker.size();
  const bool synced = written && file.sync();
  const bool closed = file.close();
  ImportMarker actual{};
  return written && synced && closed && readExact(path, actual) && actual == marker &&
         Storage.settingsChainEligible(path);
}
struct InspectContext {
  GuardState state = GuardState::Unavailable;
};
bool validCompletedSlot(unsigned slot) {
  char before[64], after[64], snapshot[64];
  if (!markerPath(PENDING, slot, before) || !markerPath(DONE, slot, after) || !snapshotPath(slot, snapshot))
    return false;
  ImportMarker a{}, b{};
  return readExact(before, a) && readExact(after, b) && valid(a, slot) && a == b &&
         Storage.lookupPath(snapshot) == HalStorage::PathStatus::Present && Storage.settingsChainEligible(snapshot) &&
         Storage.distinctSettingsChains(before, after) && Storage.distinctSettingsChains(snapshot, before) &&
         Storage.distinctSettingsChains(snapshot, after) && Storage.distinctSettingsChains(PRIMARY, snapshot) &&
         Storage.distinctSettingsChains(PRIMARY, before) && Storage.distinctSettingsChains(PRIMARY, after);
}
bool inspectLocked(void* raw) {
  auto& context = *static_cast<InspectContext*>(raw);
  uint64_t pending = 0, done = 0;
  if (!Storage.lookupNumberedChildren(DIRECTORY, PENDING, pending) ||
      !Storage.lookupNumberedChildren(DIRECTORY, DONE, done))
    return true;
  if (pending != done) {
    context.state = GuardState::NeedsReview;
    return true;
  }
  for (unsigned slot = 0; slot < 64; ++slot) {
    if (!(pending & (uint64_t{1} << slot))) continue;
    if (!validCompletedSlot(slot)) {
      context.state = GuardState::NeedsReview;
      return true;
    }
  }
  context.state = GuardState::Clear;
  return true;
}
struct BeginContext {
  const char* snapshot;
  const char* before;
  size_t beforeSize;
  const char* after;
  size_t afterSize;
  ImportMarker& marker;
  bool success = false;
};
bool beginLocked(void* raw) {
  auto& c = *static_cast<BeginContext*>(raw);
  unsigned slot = 0;
  char expected[64];
  for (; slot < 64; ++slot) {
    if (!snapshotPath(slot, expected)) return true;
    if (std::strcmp(c.snapshot, expected) == 0) break;
  }
  if (slot == 64) return true;
  const auto directory = Storage.lookupPath(DIRECTORY);
  if (directory == HalStorage::PathStatus::Failed ||
      (directory == HalStorage::PathStatus::Missing && !Storage.mkdir(DIRECTORY)))
    return true;
  char pending[64], done[64];
  if (!markerPath(PENDING, slot, pending) || !markerPath(DONE, slot, done) ||
      Storage.lookupPath(pending) != HalStorage::PathStatus::Missing ||
      Storage.lookupPath(done) != HalStorage::PathStatus::Missing ||
      Storage.lookupPath(c.snapshot) != HalStorage::PathStatus::Present ||
      !Storage.distinctSettingsChains(PRIMARY, c.snapshot))
    return true;
  ImportMarker candidate{'C', 'I', 'G', '1', static_cast<uint8_t>(slot)};
  put64(candidate, 8, hash(reinterpret_cast<const uint8_t*>(c.before), c.beforeSize));
  put64(candidate, 16, hash(reinterpret_cast<const uint8_t*>(c.after), c.afterSize));
  put64(candidate, 24, hash(candidate.data(), 24));
  if (!writeExclusive(pending, candidate) || !Storage.distinctSettingsChains(PRIMARY, pending) ||
      !Storage.distinctSettingsChains(c.snapshot, pending))
    return true;
  c.marker = candidate;
  c.success = true;
  return true;
}
bool resumeMarker(const char* path, const char* pending, const char* snapshot, const ImportMarker& marker) {
  if (!Storage.settingsChainEligible(path) || !Storage.distinctSettingsChains(PRIMARY, path) ||
      !Storage.distinctSettingsChains(snapshot, path) || !Storage.distinctSettingsChains(pending, path))
    return false;
  HalFile file = Storage.openChecked(path, O_RDWR);
  if (!file) return false;
  const uint64_t size = file.fileSize64();
  uint8_t prefix[32];
  const bool validPrefix = !file.isDirectory() && size > 0 && size <= marker.size() &&
                           file.read(prefix, static_cast<size_t>(size)) == static_cast<int>(size) &&
                           std::memcmp(prefix, marker.data(), static_cast<size_t>(size)) == 0;
  if (!validPrefix) {
    file.close();
    return false;
  }
  const bool appended =
      size == marker.size() || (file.seekSet(static_cast<size_t>(size)) &&
                                file.write(marker.data() + size, marker.size() - static_cast<size_t>(size)) ==
                                    marker.size() - static_cast<size_t>(size));
  const bool synced = appended && file.sync();
  const bool closed = file.close();
  ImportMarker actual{};
  return appended && synced && closed && readExact(path, actual) && actual == marker;
}
struct FinishContext {
  const ImportMarker& marker;
  bool allowResume = false;
  bool success = false;
};
bool finishLocked(void* raw) {
  auto& c = *static_cast<FinishContext*>(raw);
  const unsigned slot = c.marker[4];
  char pending[64], done[64], snapshot[64];
  if (!valid(c.marker, slot) || !markerPath(PENDING, slot, pending) || !markerPath(DONE, slot, done) ||
      !snapshotPath(slot, snapshot))
    return true;
  const auto doneStatus = Storage.lookupPath(done);
  if (doneStatus == HalStorage::PathStatus::Failed || (doneStatus == HalStorage::PathStatus::Present && !c.allowResume))
    return true;
  ImportMarker actual{};
  if (!readExact(pending, actual) || actual != c.marker ||
      !(doneStatus == HalStorage::PathStatus::Missing ? writeExclusive(done, c.marker)
                                                      : resumeMarker(done, pending, snapshot, c.marker)) ||
      !Storage.distinctSettingsChains(pending, done) || !Storage.distinctSettingsChains(snapshot, done) ||
      !Storage.distinctSettingsChains(PRIMARY, done))
    return true;
  c.success = true;
  return true;
}
bool primaryMatchesSnapshot(const char* snapshot, const ImportMarker& marker) {
  HalFile a = Storage.openChecked(PRIMARY, O_RDONLY), b = Storage.openChecked(snapshot, O_RDONLY);
  if (!a || !b) return false;
  const uint64_t size = a.fileSize64();
  bool same = !a.isDirectory() && !b.isDirectory() && size > 0 && size <= 32768 && b.fileSize64() == size;
  uint64_t digest = 14695981039346656037ULL;
  uint8_t first[64], second[64];
  for (size_t offset = 0; same && offset < size;) {
    const size_t count = std::min(sizeof(first), static_cast<size_t>(size) - offset);
    same = a.read(first, count) == static_cast<int>(count) && b.read(second, count) == static_cast<int>(count) &&
           std::memcmp(first, second, count) == 0;
    if (same)
      for (size_t i = 0; i < count; ++i) digest = (digest ^ first[i]) * 1099511628211ULL;
    offset += count;
  }
  const bool ac = a.close(), bc = b.close();
  ImportMarker expected = marker;
  put64(expected, 8, digest);
  return same && ac && bc && expected == marker;
}
struct RecoverContext {
  RecoveryResult result = RecoveryResult::Unavailable;
};
bool recoverLocked(void* raw) {
  auto& c = *static_cast<RecoverContext*>(raw);
  uint64_t pending = 0, done = 0;
  if (!Storage.lookupNumberedChildren(DIRECTORY, PENDING, pending) ||
      !Storage.lookupNumberedChildren(DIRECTORY, DONE, done))
    return true;
  if (done & ~pending) {
    c.result = RecoveryResult::NeedsReview;
    return true;
  }
  uint64_t unfinished = 0;
  for (unsigned i = 0; i < 64; ++i) {
    const auto bit = uint64_t{1} << i;
    if (!(pending & bit)) continue;
    if (!(done & bit) || !validCompletedSlot(i)) unfinished |= bit;
  }
  if (!unfinished) {
    if (done && Storage.lookupPath("/.crosspoint/settings.json.tmp") == HalStorage::PathStatus::Missing &&
        Storage.lookupPath("/.crosspoint/settings.json.bak") == HalStorage::PathStatus::Missing) {
      unsigned latest = 63;
      while (!(done & (uint64_t{1} << latest))) --latest;
      char pendingPath[64], snapshot[64];
      ImportMarker marker{};
      if (markerPath(PENDING, latest, pendingPath) && snapshotPath(latest, snapshot) &&
          readExact(pendingPath, marker) && valid(marker, latest) && primaryMatchesSnapshot(snapshot, marker)) {
        c.result = RecoveryResult::Recovered;
        return true;
      }
    }
    c.result = RecoveryResult::NotNeeded;
    return true;
  }
  if (unfinished & (unfinished - 1)) {
    c.result = RecoveryResult::NeedsReview;
    return true;
  }
  unsigned slot = 0;
  while (!(unfinished & (uint64_t{1} << slot))) ++slot;
  char pendingPath[64], snapshot[64];
  ImportMarker marker{};
  if (!markerPath(PENDING, slot, pendingPath) || !snapshotPath(slot, snapshot) ||
      Storage.lookupPath("/.crosspoint/settings.json.tmp") != HalStorage::PathStatus::Missing ||
      Storage.lookupPath("/.crosspoint/settings.json.bak") != HalStorage::PathStatus::Missing ||
      !readExact(pendingPath, marker) || !valid(marker, slot) || !Storage.settingsChainEligible(snapshot) ||
      !Storage.distinctSettingsChains(PRIMARY, snapshot) || !Storage.distinctSettingsChains(PRIMARY, pendingPath) ||
      !Storage.distinctSettingsChains(snapshot, pendingPath) || !primaryMatchesSnapshot(snapshot, marker)) {
    c.result = RecoveryResult::NeedsReview;
    return true;
  }
  FinishContext finish{marker, true};
  if (!finishLocked(&finish) || !finish.success) {
    c.result = RecoveryResult::NeedsReview;
    return true;
  }
  InspectContext check;
  inspectLocked(&check);
  c.result = check.state == GuardState::Clear ? RecoveryResult::Recovered : RecoveryResult::NeedsReview;
  return true;
}
}  // namespace

GuardState inspectImports() {
  InspectContext c;
  return Storage.withStorageLock(inspectLocked, &c) ? c.state : GuardState::Unavailable;
}
bool beginImport(const char* snapshot, const char* before, size_t beforeSize, const char* after, size_t afterSize,
                 ImportMarker& marker) {
  if (!snapshot || !before || !beforeSize || !after || !afterSize) return false;
  BeginContext c{snapshot, before, beforeSize, after, afterSize, marker};
  return Storage.withStorageLock(beginLocked, &c) && c.success;
}
bool finishImport(const ImportMarker& marker) {
  FinishContext c{marker};
  return Storage.withStorageLock(finishLocked, &c) && c.success;
}
RecoveryResult recoverUnchangedImport() {
  RecoverContext c;
  return Storage.withStorageLock(recoverLocked, &c) ? c.result : RecoveryResult::Unavailable;
}
}  // namespace crossinkimport
