#include <HalStorage.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>
#include <string_view>

#include "CompletionMovePlanIO.h"
#include "DeferredCompletionIntentIO.h"
#include "DeferredCompletionMove.h"

namespace readingstats::deferredcompletion {
namespace {
struct RetireMoveContext {
  const std::string& source;
  RetireMoveResult result = RetireMoveResult::Unavailable;
};

bool retireMovedLocked(void* raw) {
  auto& context = *static_cast<RetireMoveContext*>(raw);
  auto intent = makeUniqueNoThrow<Record>();
  if (!intent) return true;
  size_t intentSize = 0;
  unsigned slot = 0;
  switch (findForMoveRecovery(context.source, *intent, intentSize, slot)) {
    case FindResult::Missing:
      context.result = RetireMoveResult::Missing;
      return true;
    case FindResult::Busy:
      context.result = RetireMoveResult::Busy;
      return true;
    case FindResult::Conflict:
    case FindResult::Blocked:
      context.result = RetireMoveResult::RecoveryRequired;
      return true;
    case FindResult::Unavailable:
      return true;
    case FindResult::Found:
      break;
  }
  const auto verified = completionmove::verifyPreparedFiles();
  if (verified == completionmove::VerifyFilesResult::Unavailable) return true;
  if (verified != completionmove::VerifyFilesResult::ContentMatches) {
    context.result = RetireMoveResult::RecoveryRequired;
    return true;
  }
  auto plan = makeUniqueNoThrow<completionmove::Plan>();
  if (!plan) return true;
  size_t planSize = 0;
  std::string_view source, destination;
  if (completionmove::inspectPlan(*plan, planSize, source, destination) != completionmove::PlanResult::Ready ||
      source != context.source) {
    context.result = RetireMoveResult::RecoveryRequired;
    return true;
  }
  char path[64];
  snprintf(path, sizeof(path), "/.completion-deferred/.dretire-%02u", slot);
  Seal receipt{};
  if (!encodeSeal(slot, *intent, intentSize, receipt, true)) return true;
  const auto status = Storage.lookupPath(path);
  if (status == HalStorage::PathStatus::Present) {
    HalFile previous;
    if (!Storage.openFileForRead("STATS", path, previous)) return true;
    Seal bytes{};
    const auto length = previous.fileSize();
    const bool read = length <= bytes.size() && previous.read(bytes.data(), length) == static_cast<int>(length);
    if (!previous.close() || !read) return true;
    if (length == receipt.size() && bytes == receipt) {
      context.result = RetireMoveResult::AlreadyRetired;
      return true;
    }
    if (length >= receipt.size() || std::memcmp(bytes.data(), receipt.data(), length) != 0) {
      context.result = RetireMoveResult::RecoveryRequired;
      return true;
    }
    char archive[64];
    bool available = false;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
      snprintf(archive, sizeof(archive), "/.completion-deferred/.dbad-%02u-%02u", slot, attempt);
      const auto archiveStatus = Storage.lookupPath(archive);
      if (archiveStatus == HalStorage::PathStatus::Failed) return true;
      if (archiveStatus == HalStorage::PathStatus::Missing) {
        available = true;
        break;
      }
    }
    if (!available || !Storage.rename(path, archive) || Storage.lookupPath(path) != HalStorage::PathStatus::Missing ||
        Storage.lookupPath(archive) != HalStorage::PathStatus::Present)
      return true;
  } else if (status != HalStorage::PathStatus::Missing) {
    return true;
  }
  HalFile file = Storage.openChecked(path, O_WRONLY | O_CREAT | O_EXCL);
  if (!file) return true;
  const bool written = file.write(receipt.data(), receipt.size()) == receipt.size();
  const bool synced = file.sync();
  const bool closed = file.close();
  if (!written || !synced || !closed) return true;
  HalFile check;
  if (!Storage.openFileForRead("STATS", path, check)) return true;
  Seal actual{};
  const bool sizeMatches = check.fileSize() == actual.size();
  const bool read = sizeMatches && check.read(actual.data(), actual.size()) == static_cast<int>(actual.size());
  if (!check.close() || !read || actual != receipt) return true;
  context.result = RetireMoveResult::Retired;
  return true;
}
}  // namespace

RetireMoveResult retireMovedIntent(const std::string& source) {
  RetireMoveContext context{source};
  return Storage.withStorageLock(retireMovedLocked, &context) ? context.result : RetireMoveResult::Unavailable;
}
}  // namespace readingstats::deferredcompletion
