#pragma once

#include <strings.h>

#include <cstring>
#include <new>
#include <utility>

#include "CheckedStoragePath.h"
#include "HiddenItems.h"
#include "PathDirectoryScan.h"

namespace wirecheck {
// The owner must keep this object and its callback context alive, off a small
// task stack. Use from one task without re-entering from callbacks. A single
// snapshot spans every directory. The caller must visit every discovered
// directory accepted by includedName and discard all entries on failure.
// Rejected paths, depth, aliases, or length fail the inventory, not the book.
class InventorySession {
 public:
  using Callbacks = PathDirectoryScan::Callbacks;
  enum class State { Ready, Scanning, Failed };

  InventorySession(HalStorage& storage, Callbacks callbacks) : storage_(storage), callbacks_(callbacks) {
    if (!storage_.beginMutationSnapshot(snapshot_)) state_ = State::Failed;
  }
  ~InventorySession() { destroyScan(); }
  InventorySession(const InventorySession&) = delete;
  InventorySession& operator=(const InventorySession&) = delete;

  // Matches the host client's library exclusions for every entry type.
  static bool includedName(const char* name) {
    if (!name || !name[0] || name[0] == '.') return false;
    for (const auto* hidden : FsHelpers::HIDDEN_ITEMS)
      if (strcasecmp(name, hidden) == 0) return false;
    return true;
  }

  bool beginDirectory(const char* path) {
    if (state_ != State::Ready) return false;
    if (!validPath(path) || !storage_.mutationSnapshotUnchanged(snapshot_)) return fail();
    const size_t length = std::strlen(path);
    std::memcpy(directory_, path, length + 1);
    scan_ = new (scanMemory_) PathDirectoryScan(storage_, snapshot_, directory_, callbacks_);
    state_ = State::Scanning;
    return true;
  }

  ScanResult step() {
    if (state_ != State::Scanning) return ScanResult::Failed;
    const auto result = scan_->step();
    if (result != ScanResult::Pending) {
      destroyScan();
      state_ = result == ScanResult::Complete ? State::Ready : State::Failed;
    }
    return result;
  }

  // This checks consistency only; the caller must also prove traversal complete.
  bool snapshotValid() {
    if (state_ != State::Ready) return false;
    if (!storage_.mutationSnapshotUnchanged(snapshot_)) return fail();
    return true;
  }
  State state() const { return state_; }
  void cancel() {
    destroyScan();
    state_ = State::Failed;
  }

 private:
  bool fail() {
    state_ = State::Failed;
    return false;
  }
  void destroyScan() {
    if (scan_) {
      scan_->~PathDirectoryScan();
      scan_ = nullptr;
    }
  }
  bool validPath(const char* path) {
    if (!path || path[0] != '/') return false;
    const size_t length = strnlen(path, 2049);
    if (!length || length > 2048) return false;
    if (length == 1) return true;
    size_t offset = 1;
    unsigned depth = 0;
    while (offset < length) {
      size_t count = 0;
      while (offset < length && path[offset] != '/') {
        if (count == sizeof(component_) - 1) return false;
        component_[count++] = path[offset++];
      }
      component_[count] = '\0';
      if (!count || !includedName(component_) || !validCheckedStorageComponent(component_) || ++depth > 32)
        return false;
      // Short-name aliases can resolve to a different long name.
      for (size_t i = 0; i + 1 < count; ++i)
        if (component_[i] == '~' && component_[i + 1] >= '0' && component_[i + 1] <= '9') return false;
      if (offset < length && ++offset == length) return false;
    }
    return true;
  }

  HalStorage& storage_;
  Callbacks callbacks_;
  HalStorage::MutationSnapshot snapshot_;
  State state_ = State::Ready;
  char component_[766]{};
  char directory_[2049]{};
  alignas(PathDirectoryScan) unsigned char scanMemory_[sizeof(PathDirectoryScan)]{};
  PathDirectoryScan* scan_ = nullptr;
};
}  // namespace wirecheck
