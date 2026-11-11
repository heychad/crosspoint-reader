#include "DeviceStatsRetention.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "AutomaticBackupRetentionScan.h"
#include "DeviceStatsBackup.h"
#include "GlobalReadingStats.h"
#include "StatsAccessGate.h"
#include "StatsSessionLease.h"

namespace {
constexpr char DIRECTORY[] = "/.crossink-stats-backup";
constexpr uint32_t TIME_BUDGET_MS = 1500;
constexpr uint32_t RECORD_BUDGET = 65536;
constexpr uint8_t MAX_REMOVALS = 8;

bool completeScan(wirecheck::PathDirectoryScan& scan, uint32_t started) {
  for (uint32_t record = 0; record < RECORD_BUDGET; ++record) {
    if (static_cast<uint32_t>(millis() - started) >= TIME_BUDGET_MS) return false;
    switch (scan.step()) {
      case wirecheck::ScanResult::Complete:
        return true;
      case wirecheck::ScanResult::Failed:
        return false;
      case wirecheck::ScanResult::Pending:
        delay(0);
        break;
    }
  }
  return false;
}

struct PruneContext {
  const char* current;
  statsbackup::RetentionResult result;
  uint32_t started = millis();
  char candidate[statsbackup::AutomaticBackupRetention::NAME_SIZE + 1]{};
  char candidatePath[96]{};
  char otherPath[800]{};

  static bool keepGoing(void*) { return true; }
  static bool disjointEntry(void* raw, const wirecheck::DirectoryEntry& entry) {
    auto& self = *static_cast<PruneContext*>(raw);
    if (entry.directory) return false;
    if (std::strcmp(entry.name, self.candidate) == 0) return true;
    const int size = snprintf(self.otherPath, sizeof(self.otherPath), "%s/%s", DIRECTORY, entry.name);
    return size > 0 && size < static_cast<int>(sizeof(self.otherPath)) &&
           Storage.distinctSettingsChains(self.candidatePath, self.otherPath);
  }
};

static_assert(sizeof(PruneContext) <= 1024);
static_assert(sizeof(wirecheck::PathDirectoryScan) <= 8192);
static_assert(sizeof(statsbackup::AutomaticBackupRetentionScan) <= 8192);

bool pruneUnlocked(void* raw) {
  auto& context = *static_cast<PruneContext*>(raw);
  if (!readingstats::normalStatsAllowed() || StatsSessionLease::anyActive() ||
      Storage.lookupPath("/.crosspoint/global_stats.bin") != HalStorage::PathStatus::Present)
    return false;
  for (uint8_t attempt = 0; attempt < MAX_REMOVALS; ++attempt) {
    HalStorage::MutationSnapshot snapshot;
    if (!Storage.beginMutationSnapshot(snapshot)) return false;
    auto selection = makeUniqueNoThrow<statsbackup::AutomaticBackupRetentionScan>(Storage, snapshot, context.current);
    if (!selection) return false;
    wirecheck::ScanResult state = wirecheck::ScanResult::Pending;
    for (uint32_t record = 0; record < RECORD_BUDGET && state == wirecheck::ScanResult::Pending; ++record) {
      if (static_cast<uint32_t>(millis() - context.started) >= TIME_BUDGET_MS) return false;
      state = selection->step();
      delay(0);
    }
    if (state != wirecheck::ScanResult::Complete) return false;
    const char* candidate = selection->candidate();
    if (!candidate) return selection->count() <= statsbackup::AutomaticBackupRetention::KEEP;
    std::memcpy(context.candidate, candidate, sizeof(context.candidate));
    selection.reset();
    snprintf(context.candidatePath, sizeof(context.candidatePath), "%s/%s", DIRECTORY, context.candidate);

    auto decoded = makeUniqueNoThrow<GlobalReadingStats>();
    if (!decoded ||
        statsbackup::inspectDeviceBackup(context.current, *decoded) != statsbackup::BackupIntegrity::Checked ||
        statsbackup::inspectDeviceBackup(context.candidate, *decoded) != statsbackup::BackupIntegrity::Checked ||
        !Storage.settingsChainEligible(context.candidatePath))
      return false;
    decoded.reset();
    for (const char* path :
         {"/.crosspoint/global_stats.bin", "/.crosspoint/global_stats.bin.bak", "/.crosspoint/global_stats.bin.tmp"}) {
      const auto status = Storage.lookupPath(path);
      if (status == HalStorage::PathStatus::Failed ||
          (status == HalStorage::PathStatus::Present && !Storage.distinctSettingsChains(context.candidatePath, path)))
        return false;
    }
    // Protect every sibling, including manual, undated, and unrecognized files.
    auto siblings = makeUniqueNoThrow<wirecheck::PathDirectoryScan>(
        Storage, snapshot, DIRECTORY,
        wirecheck::PathDirectoryScan::Callbacks{&context, PruneContext::keepGoing, PruneContext::disjointEntry});
    if (!siblings || !completeScan(*siblings, context.started) || !Storage.mutationSnapshotUnchanged(snapshot))
      return false;
    siblings.reset();
    if (!Storage.remove(context.candidatePath)) return false;
    ++context.result.removed;
  }
  // A larger backlog is deliberately handled on subsequent sleeps.
  return false;
}
}  // namespace

statsbackup::RetentionResult statsbackup::pruneAutomaticDeviceBackups(const char* current) {
  if (!AutomaticBackupRetention::validName(current)) return {};
  auto context = makeUniqueNoThrow<PruneContext>();
  if (!context) return {0, true};
  context->current = current;
  context->result.deferred = !Storage.withStorageLock(pruneUnlocked, context.get());
  return context->result;
}

namespace {
struct SequenceContext {
  char group;
  uint32_t maximum = 0;
  char latest[statsbackup::AutomaticBackupRetention::NAME_SIZE + 1]{};
  static bool keepGoing(void*) { return true; }
  static bool observe(void* raw, const wirecheck::DirectoryEntry& entry) {
    auto& self = *static_cast<SequenceContext*>(raw);
    if (entry.directory || statsbackup::AutomaticBackupRetention::group(entry.name) != self.group) return true;
    uint32_t value = 0;
    if (!statsbackup::AutomaticBackupRetention::sequence(entry.name, value)) return false;
    if (value > self.maximum || (value == self.maximum && std::strcmp(entry.name, self.latest) > 0)) {
      self.maximum = value;
      std::memcpy(self.latest, entry.name, std::strlen(entry.name) + 1);
    }
    return true;
  }
};
}  // namespace

bool statsbackup::nextDeviceBackupSequence(char group, uint32_t& next, char* latest, size_t latestSize) {
  if ((group != 'M' && group != 'U') || (latest && latestSize < AutomaticBackupRetention::NAME_SIZE + 1)) return false;
  struct Context {
    char group;
    uint32_t next = 0;
    char latest[AutomaticBackupRetention::NAME_SIZE + 1]{};
  } context{group};
  const auto scanLocked = [](void* raw) {
    auto& output = *static_cast<Context*>(raw);
    HalStorage::MutationSnapshot snapshot;
    if (!Storage.beginMutationSnapshot(snapshot)) return false;
    SequenceContext state{output.group};
    auto scan = makeUniqueNoThrow<wirecheck::PathDirectoryScan>(
        Storage, snapshot, DIRECTORY,
        wirecheck::PathDirectoryScan::Callbacks{&state, SequenceContext::keepGoing, SequenceContext::observe});
    if (!scan || !completeScan(*scan, millis()) || !Storage.mutationSnapshotUnchanged(snapshot) ||
        state.maximum == UINT32_MAX)
      return false;
    output.next = state.maximum + 1;
    std::memcpy(output.latest, state.latest, sizeof(output.latest));
    return true;
  };
  if (!Storage.withStorageLock(scanLocked, &context)) return false;
  next = context.next;
  if (latest) std::memcpy(latest, context.latest, sizeof(context.latest));
  return true;
}
