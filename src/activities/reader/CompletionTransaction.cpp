#include "CompletionTransaction.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Memory.h>

#include <functional>
#include <string>

#include "BookReadingStats.h"
#include "CompletionEdit.h"
#include "CompletionJournalIO.h"
#include "CompletionMovePlanIO.h"
#include "CompletionSnapshotValidation.h"
#include "DeferredCompletionIntentIO.h"
#include "GlobalReadingStats.h"
#include "ReadingStatsStore.h"
#include "StatsSessionLease.h"

namespace readingstats {
namespace {
namespace cj = completionjournal;

struct Work {
  cj::Baseline baseline;
  cj::Book nextBook{};
  cj::Global nextGlobal{};
  cj::Record record{};
};

std::string cachePath(const std::string& bookPath) {
  const char* prefix = nullptr;
  if (FsHelpers::hasReflowableBookExtension(bookPath))
    prefix = "epub_";
  else if (FsHelpers::hasXtcExtension(bookPath))
    prefix = "xtc_";
  if (!prefix) return {};
  return std::string("/.crosspoint/") + prefix + std::to_string(std::hash<std::string>{}(bookPath));
}

bool capture(const std::string& path, uint8_t* bytes, size_t expected) {
  if (Storage.lookupPath(path.c_str()) != HalStorage::PathStatus::Present) return false;
  HalFile file;
  if (!Storage.openFileForRead("STATS", path, file)) return false;
  const bool correctSize = file.fileSize() == expected;
  const bool read = correctSize && file.read(bytes, expected) == static_cast<int>(expected);
  const bool closed = file.close();
  return read && closed;
}

struct Context {
  const std::string& bookPath;
  bool completed;
  const ReadingStatsDate& today;
  uint8_t policy;
  const std::string* expectedCache = nullptr;
  const cj::Book* expectedCurrent = nullptr;
  const cj::Book* replacement = nullptr;
  const uint64_t* deferredOrigin = nullptr;
  ClosedCompletionResult result = ClosedCompletionResult::Unavailable;
};

bool setClosedLocked(void* raw) {
  auto& context = *static_cast<Context*>(raw);
  if (StatsSessionLease::anyActive()) {
    context.result = ClosedCompletionResult::Busy;
    return true;
  }
  if ((context.policy & ~cj::POLICY_MASK) || (!context.completed && context.policy) ||
      static_cast<bool>(context.policy & cj::MOVE_FROM_DEFERRED) != static_cast<bool>(context.deferredOrigin)) {
    context.result = ClosedCompletionResult::Invalid;
    return true;
  }
  const std::string cache = cachePath(context.bookPath);
  if (cache.empty() || (context.expectedCache && cache != *context.expectedCache)) {
    context.result = ClosedCompletionResult::Invalid;
    return true;
  }
  if (Storage.lookupPath(context.bookPath.c_str()) != HalStorage::PathStatus::Present) return true;
  const std::string directory = bookDirectory(cache);
  const std::string primary = directory + "/stats_v5.bin";
  auto work = makeUniqueNoThrow<Work>();
  if (!work) return true;
  size_t pendingSize = 0;
  unsigned pendingSlot = 0;
  const auto pending = cj::scanRecords(work->record, pendingSize, pendingSlot);
  if (pending == cj::ScanResult::Candidate || pending == cj::ScanResult::Conflict ||
      pending == cj::ScanResult::Damaged) {
    context.result = ClosedCompletionResult::RecoveryRequired;
    return true;
  }
  if (pending == cj::ScanResult::Unavailable) return true;
  if (!capture(primary, work->baseline.book.data(), work->baseline.book.size()) ||
      !capture("/.crosspoint/global_stats.bin", work->baseline.global.data(), work->baseline.global.size()) ||
      !cj::validSnapshotPayload(work->baseline.book.data(), work->baseline.global.data()))
    return true;
  work->baseline.bookSize = work->baseline.book.size();
  work->baseline.globalSize = work->baseline.global.size();
  if (context.replacement && (!context.expectedCurrent || *context.expectedCurrent != work->baseline.book ||
                              !cj::validSnapshotPayload(context.replacement->data(), work->baseline.global.data()))) {
    context.result = ClosedCompletionResult::Invalid;
    return true;
  }
  bool enabled = false, found = false, known = false;
  auto book = loadBook(cache, context.bookPath, enabled, &found, BookReadingStats::LoadMode::TransactionalEdit, &known);
  auto global = GlobalReadingStats::loadReadOnly();
  if (!known || !found || !book.writable || !global.writable) return true;
  const auto change = applyCompletion(book, global, context.completed, context.today);
  if (context.replacement) {
    BookReadingStats decoded;
    cj::Book encoded{};
    if (change != CompletionChange::Changed ||
        !BookReadingStats::decode(context.replacement->data(), context.replacement->size(), decoded) ||
        decoded.isCompleted != context.completed || !decoded.encode(encoded.data(), encoded.size()) ||
        encoded != *context.replacement || !global.encode(work->nextGlobal)) {
      context.result = ClosedCompletionResult::Invalid;
      return true;
    }
    work->nextBook = *context.replacement;
  } else if (change == CompletionChange::Unchanged) {
    if (!context.policy) {
      context.result = ClosedCompletionResult::Unchanged;
      return true;
    }
    work->nextBook = work->baseline.book;
    work->nextGlobal = work->baseline.global;
  } else if (change != CompletionChange::Changed || !book.encode(work->nextBook.data(), work->nextBook.size()) ||
             !global.encode(work->nextGlobal)) {
    return true;
  }
  size_t size = 0;
  const std::string_view deferredSource = (context.policy & (cj::DEFER_MOVE_TO_READ | cj::MOVE_FROM_DEFERRED))
                                              ? std::string_view(context.bookPath)
                                              : std::string_view{};
  if (!cj::encode(directory, work->nextBook, work->nextGlobal, work->baseline, work->record, size, context.policy,
                  deferredSource, context.deferredOrigin ? *context.deferredOrigin : 0))
    return true;
  const auto journal = Storage.lookupPath("/.completion-journal");
  if (journal == HalStorage::PathStatus::Failed) return true;
  if (journal == HalStorage::PathStatus::Missing &&
      (!Storage.mkdir("/.completion-journal", false) ||
       Storage.lookupPath("/.completion-journal") != HalStorage::PathStatus::Present))
    return true;
  const auto started = cj::start(work->record, size);
  if (started != cj::StartResult::Started) {
    if (started == cj::StartResult::NeedsRecovery || started == cj::StartResult::Uncertain ||
        started == cj::StartResult::Blocked)
      context.result = ClosedCompletionResult::RecoveryRequired;
    return true;
  }
  if (cj::replay() != cj::ReplayResult::AppliedUnretired) {
    context.result = ClosedCompletionResult::RecoveryRequired;
    return true;
  }
  context.result = ClosedCompletionResult::Applied;
  return true;
}

struct RecoveryContext {
  const std::string& bookPath;
  bool completed;
  CompletionRecoveryResult result = CompletionRecoveryResult::Unavailable;
};

struct ProbeContext {
  const std::string& bookPath;
  bool& target;
  uint8_t* policy = nullptr;
  CompletionProbeResult result = CompletionProbeResult::Unavailable;
};

// A move plan retains the original path after the EPUB itself is renamed.
// Accept either path only when exactly one copy exists and the sealed plan
// binds that path to this pending completion.
bool matchesPendingBook(const std::string& bookPath, std::string_view directory, std::string_view boundSource = {}) {
  namespace cm = completionmove;
  auto plan = makeUniqueNoThrow<cm::Plan>();
  if (!plan) return false;
  size_t size = 0;
  std::string_view from, to;
  auto identities = makeUniqueNoThrow<std::array<cm::FileIdentity, cm::FILE_COUNT>>();
  if (!identities) return false;
  const auto state = cm::inspectPlan(*plan, size, from, to, identities->data());
  if (state == cm::PlanResult::Ready) {
    if ((bookPath != from && bookPath != to) || (!boundSource.empty() && from != boundSource) ||
        !(*identities)[0].present)
      return false;
    const auto source = Storage.lookupPath(std::string(from).c_str());
    const auto target = Storage.lookupPath(std::string(to).c_str());
    const bool atSource = source == HalStorage::PathStatus::Present && target == HalStorage::PathStatus::Missing;
    const bool atTarget = source == HalStorage::PathStatus::Missing && target == HalStorage::PathStatus::Present;
    if (!atSource && !atTarget) return false;
    cm::FileIdentity actual;
    if (cm::captureFile(std::string(atSource ? from : to), actual) != cm::FileResult::Ready) return false;
    return actual.size == (*identities)[0].size && actual.checksum == (*identities)[0].checksum;
  }
  if (state != cm::PlanResult::Missing) return false;
  if (!boundSource.empty() && bookPath != boundSource) return false;
  const std::string cache = cachePath(bookPath);
  return !cache.empty() && directory == bookDirectory(cache) &&
         Storage.lookupPath(bookPath.c_str()) == HalStorage::PathStatus::Present;
}

bool probeClosedLocked(void* raw) {
  auto& context = *static_cast<ProbeContext*>(raw);
  auto record = makeUniqueNoThrow<cj::Record>();
  if (!record) return true;
  size_t size = 0;
  unsigned slot = 0;
  const auto state = cj::scanRecords(*record, size, slot);
  if (state == cj::ScanResult::Empty || state == cj::ScanResult::PreparedOnly) {
    context.result = CompletionProbeResult::NoPending;
    return true;
  }
  if (state == cj::ScanResult::Conflict || state == cj::ScanResult::Damaged) {
    context.result = CompletionProbeResult::ManualReview;
    return true;
  }
  if (state != cj::ScanResult::Candidate) return true;
  std::string_view directory;
  const uint8_t *book = nullptr, *global = nullptr;
  if (!cj::decode(record->data(), size, directory, book, global)) {
    context.result = CompletionProbeResult::ManualReview;
    return true;
  }
  std::string_view deferredSource;
  if (((*record)[9] & cj::DEFER_MOVE_TO_READ) && (!cj::decodeDeferredSource(record->data(), size, deferredSource) ||
                                                  deferredSource != context.bookPath || book[11] == 0)) {
    context.result = CompletionProbeResult::DifferentTransaction;
    return true;
  }
  std::string_view moveSource;
  uint64_t originHash = 0;
  if (((*record)[9] & cj::MOVE_FROM_DEFERRED) &&
      (!cj::decodeDeferredMove(record->data(), size, moveSource, originHash) || book[11] == 0)) {
    context.result = CompletionProbeResult::DifferentTransaction;
    return true;
  }
  if (!matchesPendingBook(context.bookPath, directory, moveSource)) {
    context.result = CompletionProbeResult::DifferentTransaction;
    return true;
  }
  context.target = book[11] != 0;
  if (context.policy) *context.policy = (*record)[9];
  context.result = CompletionProbeResult::Matching;
  return true;
}

bool recoverClosedLocked(void* raw) {
  auto& context = *static_cast<RecoveryContext*>(raw);
  if (StatsSessionLease::anyActive()) {
    context.result = CompletionRecoveryResult::Busy;
    return true;
  }
  auto record = makeUniqueNoThrow<cj::Record>();
  if (!record) return true;
  size_t size = 0;
  unsigned slot = 0;
  const auto state = cj::scanRecords(*record, size, slot);
  if (state == cj::ScanResult::Empty || state == cj::ScanResult::PreparedOnly) {
    context.result = CompletionRecoveryResult::NoPending;
    return true;
  }
  if (state == cj::ScanResult::Conflict || state == cj::ScanResult::Damaged) {
    context.result = CompletionRecoveryResult::ManualReview;
    return true;
  }
  if (state != cj::ScanResult::Candidate) return true;
  std::string_view directory;
  const uint8_t *book = nullptr, *global = nullptr;
  if (!cj::decode(record->data(), size, directory, book, global)) {
    context.result = CompletionRecoveryResult::ManualReview;
    return true;
  }
  std::string_view deferredSource;
  if (((*record)[9] & cj::DEFER_MOVE_TO_READ) && (!cj::decodeDeferredSource(record->data(), size, deferredSource) ||
                                                  deferredSource != context.bookPath || book[11] == 0)) {
    context.result = CompletionRecoveryResult::DifferentTransaction;
    return true;
  }
  std::string_view moveSource;
  uint64_t originHash = 0;
  if (((*record)[9] & cj::MOVE_FROM_DEFERRED) &&
      (!cj::decodeDeferredMove(record->data(), size, moveSource, originHash) || book[11] == 0)) {
    context.result = CompletionRecoveryResult::DifferentTransaction;
    return true;
  }
  if (!matchesPendingBook(context.bookPath, directory, moveSource) || (book[11] != 0) != context.completed) {
    context.result = CompletionRecoveryResult::DifferentTransaction;
    return true;
  }
  if (cj::replay() != cj::ReplayResult::AppliedUnretired) {
    context.result = CompletionRecoveryResult::ManualReview;
    return true;
  }
  context.result = CompletionRecoveryResult::Recovered;
  return true;
}

struct FinalizeContext {
  const std::string& bookPath;
  bool completed;
  CompletionFinalizeResult result = CompletionFinalizeResult::Unavailable;
};

bool finalizeClosedLocked(void* raw) {
  auto& context = *static_cast<FinalizeContext*>(raw);
  if (StatsSessionLease::anyActive()) {
    context.result = CompletionFinalizeResult::Busy;
    return true;
  }
  bool target = false;
  uint8_t policy = 0;
  ProbeContext probe{context.bookPath, target, &policy};
  probeClosedLocked(&probe);
  switch (probe.result) {
    case CompletionProbeResult::NoPending:
      context.result = CompletionFinalizeResult::NoPending;
      return true;
    case CompletionProbeResult::DifferentTransaction:
      context.result = CompletionFinalizeResult::DifferentTransaction;
      return true;
    case CompletionProbeResult::ManualReview:
      context.result = CompletionFinalizeResult::ManualReview;
      return true;
    case CompletionProbeResult::Unavailable:
      return true;
    case CompletionProbeResult::Matching:
      break;
  }
  if (target != context.completed) {
    context.result = CompletionFinalizeResult::DifferentTransaction;
    return true;
  }
  // A moved book needs durable proof that every file and reference was migrated.
  // Until that proof exists, preserve the journal even if the caller asks to retire it.
  if (policy & cj::MOVE_TO_READ) {
    context.result = CompletionFinalizeResult::ManualReview;
    return true;
  }
  if (policy & cj::DEFER_MOVE_TO_READ) {
    const auto published = deferredcompletion::publish(context.bookPath);
    if (published != deferredcompletion::PublishResult::Published &&
        published != deferredcompletion::PublishResult::AlreadyPublished) {
      context.result = CompletionFinalizeResult::ManualReview;
      return true;
    }
  }
  context.result = cj::retire() == cj::RetirementResult::Retired ? CompletionFinalizeResult::Finalized
                                                                 : CompletionFinalizeResult::ManualReview;
  return true;
}
}  // namespace

bool completionCacheMatchesBook(const std::string& bookPath, const std::string& cache) {
  const auto expected = cachePath(bookPath);
  return !expected.empty() && expected == cache;
}

ClosedCompletionResult setClosedBookCompletionStats(const std::string& bookPath, bool completed,
                                                    const ReadingStatsDate& today, uint8_t policy) {
  Context context{bookPath, completed, today, policy};
  return Storage.withStorageLock(setClosedLocked, &context) ? context.result : ClosedCompletionResult::Unavailable;
}

ClosedCompletionResult setClosedBookDeferredMoveStats(const std::string& bookPath, const ReadingStatsDate& today,
                                                      uint64_t originHash, bool removeRecent) {
  const uint8_t policy = cj::MOVE_TO_READ | cj::MOVE_FROM_DEFERRED | (removeRecent ? cj::REMOVE_RECENTS : 0);
  Context context{bookPath, true, today, policy};
  context.deferredOrigin = &originHash;
  return Storage.withStorageLock(setClosedLocked, &context) ? context.result : ClosedCompletionResult::Unavailable;
}

ClosedCompletionResult restoreClosedBookSnapshot(const std::string& bookPath, const std::string& expectedCache,
                                                 const completionjournal::Book& expectedCurrent,
                                                 const completionjournal::Book& replacement) {
  const ReadingStatsDate noDate{};
  Context context{bookPath, replacement[11] != 0, noDate, 0, &expectedCache, &expectedCurrent, &replacement};
  return Storage.withStorageLock(setClosedLocked, &context) ? context.result : ClosedCompletionResult::Unavailable;
}

CompletionRecoveryResult recoverClosedBookCompletionStats(const std::string& bookPath, bool completed) {
  RecoveryContext context{bookPath, completed};
  return Storage.withStorageLock(recoverClosedLocked, &context) ? context.result
                                                                : CompletionRecoveryResult::Unavailable;
}

CompletionProbeResult probeClosedBookCompletion(const std::string& bookPath, bool& target, uint8_t* policy) {
  ProbeContext context{bookPath, target, policy};
  return Storage.withStorageLock(probeClosedLocked, &context) ? context.result : CompletionProbeResult::Unavailable;
}

CompletionFinalizeResult finalizeClosedBookCompletion(const std::string& bookPath, bool completed) {
  FinalizeContext context{bookPath, completed};
  return Storage.withStorageLock(finalizeClosedLocked, &context) ? context.result
                                                                 : CompletionFinalizeResult::Unavailable;
}
}  // namespace readingstats
