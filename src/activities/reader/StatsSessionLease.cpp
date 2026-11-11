#include "StatsSessionLease.h"

#include <HalStorage.h>

#include <cstdint>
#include <limits>

#include "StatsAccessGate.h"

namespace {
uint32_t activeSessions = 0;
struct Change {
  bool* held;
  bool acquired = false;
};
bool acquireLocked(void* raw) {
  auto& change = *static_cast<Change*>(raw);
  if (*change.held) {
    change.acquired = true;
    return true;
  }
  if (!readingstats::normalStatsAllowed()) return true;
  if (activeSessions < std::numeric_limits<uint32_t>::max()) {
    ++activeSessions;
    *change.held = true;
    change.acquired = true;
  }
  return true;
}
bool releaseLocked(void* raw) {
  auto& held = *static_cast<bool*>(raw);
  if (held && activeSessions) {
    --activeSessions;
    held = false;
  }
  return true;
}
bool anyLocked(void* raw) {
  *static_cast<bool*>(raw) = activeSessions != 0;
  return true;
}
struct Suspension {
  bool* held;
  bool (*operation)(void*);
  void* context;
  StatsSessionLease::SuspensionResult result = StatsSessionLease::SuspensionResult::Unavailable;
};
bool suspendLocked(void* raw) {
  auto& suspension = *static_cast<Suspension*>(raw);
  if (!*suspension.held) {
    suspension.result = StatsSessionLease::SuspensionResult::NotHeld;
    return true;
  }
  if (activeSessions != 1) {
    suspension.result = StatsSessionLease::SuspensionResult::Busy;
    return true;
  }
  --activeSessions;
  *suspension.held = false;
  const bool succeeded = suspension.operation(suspension.context);
  if (activeSessions != 0 || !readingstats::normalStatsAllowed()) {
    suspension.result = StatsSessionLease::SuspensionResult::RecoveryRequired;
    return true;
  }
  ++activeSessions;
  *suspension.held = true;
  suspension.result =
      succeeded ? StatsSessionLease::SuspensionResult::Completed : StatsSessionLease::SuspensionResult::OperationFailed;
  return true;
}
}  // namespace

StatsSessionLease::~StatsSessionLease() { release(); }

bool StatsSessionLease::acquire() {
  Change change{&held};
  return Storage.withStorageLock(acquireLocked, &change) && change.acquired;
}

bool StatsSessionLease::release() {
  if (!held) return true;
  return Storage.withStorageLock(releaseLocked, &held) && !held;
}

StatsSessionLease::SuspensionResult StatsSessionLease::withExclusiveSuspension(bool (*operation)(void*),
                                                                               void* context) {
  if (!operation) return SuspensionResult::Unavailable;
  Suspension suspension{&held, operation, context};
  return Storage.withStorageLock(suspendLocked, &suspension) ? suspension.result : SuspensionResult::Unavailable;
}

bool StatsSessionLease::anyActive() {
  bool active = true;
  return !Storage.withStorageLock(anyLocked, &active) || active;
}
