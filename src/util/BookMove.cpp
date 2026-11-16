#include "BookMove.h"

#include <HalStorage.h>
#include <Memory.h>

#include "BookMoveStorage.h"
#include "CrossPointState.h"
#include "RecentBooksStore.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "activities/reader/CompletionJournalIO.h"
#include "activities/reader/StatsSessionLease.h"
#include "network/sync/StoredReferenceGuard.h"

namespace bookmove {
namespace {
bool proceed(void*) { return true; }

bool memoryClear(const std::string& path) {
  if (syncpublish::referenceMayReferTo(APP_STATE.openEpubPath, path)) return false;
  for (const auto& book : RECENT_BOOKS.getBooks())
    if (syncpublish::referenceMayReferTo(book.path, path)) return false;
  return true;
}

bool referencesClear(const std::string& path) {
  return memoryClear(path) && syncpublish::inspectStoredSourceReferences(Storage, path, proceed, nullptr) ==
                                  syncpublish::ReferenceGuardResult::Clear;
}

bool completionIdle() {
  const auto state = Storage.lookupPath("/.completion-journal");
  if (state == HalStorage::PathStatus::Missing) return true;
  if (state != HalStorage::PathStatus::Present) return false;
  auto record = makeUniqueNoThrow<readingstats::completionjournal::Record>();
  if (!record) return false;
  size_t size = 0;
  unsigned slot = 0;
  return readingstats::completionjournal::scanRecords(*record, size, slot) ==
         readingstats::completionjournal::ScanResult::Empty;
}

struct Request {
  const std::string* from = nullptr;
  const std::string* to = nullptr;
  Result result = Result::Unavailable;
};

bool dispatch(void* raw) {
  auto& request = *static_cast<Request*>(raw);
  if (StatsSessionLease::anyActive() || !completionIdle()) {
    request.result = Result::Busy;
    return true;
  }
  auto work = makeUniqueNoThrow<Work>();
  if (!work) return true;
  if (pending()) {
    request.result = inspect(*work);
    if (request.result != Result::Moved) return true;
    if (request.from && (work->from != *request.from || work->to != *request.to)) {
      request.result = Result::RecoveryRequired;
      return true;
    }
  } else {
    if (!request.from) {
      request.result = Result::NothingPending;
      return true;
    }
    work->from = *request.from;
    work->to = *request.to;
    request.result = prepare(*work);
    if (request.result != Result::Moved) return true;
  }
  request.result = execute(*work);
  return true;
}

Result run(Request& request) {
  if (!activityManager.isMainTask()) return Result::Busy;
  RenderLock render(RenderLock::Mode::Try);
  if (!render.ownsLock()) return Result::Busy;
  return Storage.withStorageLock(dispatch, &request) ? request.result : Result::Unavailable;
}
}  // namespace

bool destinationReferencesClear(const Work& work) { return referencesClear(work.to); }

bool updateReferences(const Work& work) {
  auto oldCache = rootPath(work.from, work.kind, Root::Cache) + "/";
  auto newCache = rootPath(work.to, work.kind, Root::Cache) + "/";
  const auto legacy = rootPath(work.from, work.kind, Root::LegacyCache) + "/";
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    if (book.path == work.from && book.coverBmpPath.rfind(legacy, 0) == 0) {
      oldCache = legacy;
      newCache = rootPath(work.to, work.kind, Root::LegacyCache) + "/";
      break;
    }
  }
  if (!RECENT_BOOKS.updatePathPersisted(work.from, work.to, oldCache, newCache) ||
      !APP_STATE.updateOpenEpubPathPersisted(work.from, work.to))
    return false;
  return referencesClear(work.from);
}

Result move(const std::string& from, const std::string& to) {
  Request request{&from, &to};
  return run(request);
}

Result recover() {
  Request request;
  return run(request);
}
}  // namespace bookmove
