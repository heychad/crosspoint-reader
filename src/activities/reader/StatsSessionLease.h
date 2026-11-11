#pragma once

// Coordinates live reader snapshots with closed-book statistics edits. All
// counter access is serialized by HalStorage's recursive storage lock.
class StatsSessionLease {
 public:
  StatsSessionLease() = default;
  ~StatsSessionLease();
  StatsSessionLease(const StatsSessionLease&) = delete;
  StatsSessionLease& operator=(const StatsSessionLease&) = delete;
  bool acquire();
  // False means the lease remains held; the caller must not begin a closed-book edit.
  bool release();
  enum class SuspensionResult { Completed, OperationFailed, Busy, NotHeld, RecoveryRequired, Unavailable };
  // Runs one synchronous edit while the caller's sole lease is suspended and
  // the recursive storage lock is held. RecoveryRequired leaves it released;
  // callers must stop the live session until recovery and reacquisition.
  SuspensionResult withExclusiveSuspension(bool (*operation)(void*), void* context);
  // Returns true when a session exists or the registry cannot be checked.
  static bool anyActive();

 private:
  bool held = false;
};
