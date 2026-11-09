#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

// Experimental model only. Apart from session ID allocation, every method
// requires the same storage mutex. It cannot observe unreported mutations.
// Snapshots and leases must not be retained across a reboot.
namespace checkedscan {
class StorageMutationFence {
 public:
  class Snapshot {
    friend class StorageMutationFence;
    uint64_t epoch = 0;
    uint32_t session = 0;
    const StorageMutationFence* owner = nullptr;
  };
  // Unlike a snapshot, a marker can be observed while a writer is open. It
  // detects any intervening HAL mutation; it does not reserve a file path.
  class Marker {
    friend class StorageMutationFence;
    uint64_t epoch = 0;
    uint32_t session = 0;
    const StorageMutationFence* owner = nullptr;
  };
  // A non-copyable lease stays with one writable file handle. Its owner must
  // close the handle and report writerClosed under the same storage mutex.
  class WriterLease {
    friend class StorageMutationFence;
    uint32_t session = 0;
    const StorageMutationFence* owner = nullptr;

   public:
    WriterLease() = default;
    WriterLease(const WriterLease&) = delete;
    WriterLease& operator=(const WriterLease&) = delete;
  };
  explicit StorageMutationFence(uint64_t initialEpoch = 0) : epoch(initialEpoch), session(allocateSession()) {
    if (!session) poisoned = true;
  }
  StorageMutationFence(const StorageMutationFence&) = delete;
  StorageMutationFence& operator=(const StorageMutationFence&) = delete;

  // Call before any operation that might mutate storage, even if it fails.
  void mutationAttempt() { bump(); }
  // A failed close or uncertain storage state invalidates every snapshot.
  void poison() { poisoned = true; }
  bool healthy() const { return !poisoned; }
  bool captureMarker(Marker& out) const {
    out.owner = nullptr;
    if (poisoned) return false;
    out.epoch = epoch;
    out.session = session;
    out.owner = this;
    return true;
  }
  bool markerUnchanged(const Marker& marker) const {
    return marker.owner == this && marker.session == session && !poisoned && marker.epoch == epoch;
  }
  // Register a successfully opened writable handle while still under the lock.
  bool writerOpened(WriterLease& lease) {
    bump();
    if (lease.owner || poisoned || writers == std::numeric_limits<uint32_t>::max()) {
      poisoned = true;
      return false;
    }
    lease.owner = this;
    lease.session = session;
    ++writers;
    return true;
  }
  // Call after the writable handle has closed, while still under the lock.
  bool writerClosed(WriterLease& lease) {
    bump();
    if (lease.owner != this || lease.session != session || !writers || poisoned) {
      poisoned = true;
      return false;
    }
    lease.owner = nullptr;
    lease.session = 0;
    --writers;
    return true;
  }
  bool beginSnapshot(Snapshot& out) const {
    out.owner = nullptr;
    if (poisoned || writers) return false;
    out.epoch = epoch;
    out.session = session;
    out.owner = this;
    return true;
  }
  bool unchanged(const Snapshot& snapshot) const {
    return snapshot.owner == this && snapshot.session == session && !poisoned && !writers && snapshot.epoch == epoch;
  }

 private:
  static uint32_t allocateSession() {
    uint32_t current = sessions.load(std::memory_order_relaxed);
    do {
      if (current == std::numeric_limits<uint32_t>::max()) return 0;
    } while (!sessions.compare_exchange_weak(current, current + 1, std::memory_order_relaxed));
    return current + 1;
  }
  void bump() {
    if (epoch == std::numeric_limits<uint64_t>::max()) {
      poisoned = true;
      return;
    }
    ++epoch;
  }
  inline static std::atomic<uint32_t> sessions{0};
  uint64_t epoch = 0;
  uint32_t session = 0;
  uint32_t writers = 0;
  bool poisoned = false;
};
}  // namespace checkedscan
