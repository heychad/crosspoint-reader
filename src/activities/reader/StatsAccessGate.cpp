#include "StatsAccessGate.h"

#include <HalStorage.h>
#include <Memory.h>

#include "CompletionJournalIO.h"

bool readingstats::normalStatsAllowed() {
  const auto journal = Storage.lookupPath("/.completion-journal");
  if (journal == HalStorage::PathStatus::Missing) return true;
  if (journal != HalStorage::PathStatus::Present) return false;
  auto record = makeUniqueNoThrow<completionjournal::Record>();
  if (!record) return false;
  size_t size = 0;
  unsigned slot = 0;
  const auto state = completionjournal::scanRecords(*record, size, slot);
  return state == completionjournal::ScanResult::Empty || state == completionjournal::ScanResult::PreparedOnly;
}
