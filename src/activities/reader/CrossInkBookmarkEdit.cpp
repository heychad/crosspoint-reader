#include "CrossInkBookmarkEdit.h"

#include <HalStorage.h>

#include <algorithm>
#include <cstring>

#include "CrossInkBookmarkMove.h"

namespace legacybookmarkedit {
namespace {
namespace codec = readingstats::completionmove;
struct Context {
  const std::string& source;
  const std::string& bookPath;
  uint16_t first;
  uint16_t count;
  bool publish;
  Result result = Result::Unavailable;
};

bool copyOriginal(HalFile& source, HalFile& guard) {
  if (!source.seekSet(0) || !guard.seekSet(0) || guard.fileSize() != 0) return false;
  const size_t length = source.fileSize();
  uint8_t bytes[256];
  for (size_t offset = 0; offset < length;) {
    const size_t count = std::min(sizeof(bytes), length - offset);
    if (source.read(bytes, count) != static_cast<int>(count) || guard.write(bytes, count) != count) return false;
    offset += count;
  }
  return guard.fileSize() == length && guard.sync();
}

bool sameBytes(HalFile& first, HalFile& second) {
  if (first.fileSize() != second.fileSize() || !first.seekSet(0) || !second.seekSet(0)) return false;
  const size_t length = first.fileSize();
  uint8_t a[256], b[256];
  for (size_t offset = 0; offset < length;) {
    const size_t count = std::min(sizeof(a), length - offset);
    if (first.read(a, count) != static_cast<int>(count) || second.read(b, count) != static_cast<int>(count) ||
        std::memcmp(a, b, count) != 0)
      return false;
    offset += count;
  }
  return true;
}

bool createGuard(const std::string& source, const std::string& staged, const std::string& guard) {
  HalFile original = Storage.openChecked(source.c_str(), O_RDONLY);
  if (!original || original.isDirectory()) return false;
  HalFile copy = Storage.openChecked(guard.c_str(), O_RDWR | O_CREAT | O_EXCL);
  if (!copy || copy.isDirectory()) {
    original.close();
    return false;
  }
  const bool independent = Storage.distinctMoveChains(source.c_str(), guard.c_str()) &&
                           Storage.distinctMoveChains(staged.c_str(), guard.c_str());
  const bool copied = independent && copyOriginal(original, copy) && sameBytes(original, copy);
  const bool copyClosed = copy.close();
  const bool originalClosed = original.close();
  if (!copied || !copyClosed || !originalClosed || !Storage.distinctMoveChains(source.c_str(), guard.c_str()) ||
      !Storage.distinctMoveChains(staged.c_str(), guard.c_str()))
    return false;
  original = Storage.openChecked(source.c_str(), O_RDONLY);
  copy = Storage.openChecked(guard.c_str(), O_RDONLY);
  const bool verified = original && copy && sameBytes(original, copy);
  const bool finalCopyClosed = copy.close();
  const bool finalOriginalClosed = original.close();
  return verified && finalCopyClosed && finalOriginalClosed;
}

bool verifyPublished(const Context& c, const std::string& backup, const std::string& guard) {
  if (Storage.lookupPath(c.source.c_str()) != HalStorage::PathStatus::Present ||
      Storage.lookupPath(backup.c_str()) != HalStorage::PathStatus::Present ||
      Storage.lookupPath(guard.c_str()) != HalStorage::PathStatus::Present ||
      !Storage.distinctMoveChains(c.source.c_str(), backup.c_str()) ||
      !Storage.distinctMoveChains(c.source.c_str(), guard.c_str()) ||
      !Storage.distinctMoveChains(backup.c_str(), guard.c_str()))
    return false;
  HalFile current = Storage.openChecked(c.source.c_str(), O_RDONLY);
  HalFile original = Storage.openChecked(backup.c_str(), O_RDONLY);
  HalFile copy = Storage.openChecked(guard.c_str(), O_RDONLY);
  bool complete = false;
  const bool verified = current && original && copy && sameBytes(original, copy) &&
                        codec::compareBookmarkRemoval(original, current, c.bookPath, c.first, c.count, complete) ==
                            codec::BookmarkRewriteResult::Ready &&
                        complete;
  const bool currentClosed = current.close();
  const bool originalClosed = original.close();
  const bool copyClosed = copy.close();
  return verified && currentClosed && originalClosed && copyClosed;
}

bool archive(void* raw) {
  auto& c = *static_cast<Context*>(raw);
  const std::string staged = c.source + ".bookmark-edit.tmp";
  const std::string backup = c.source + ".bookmark-edit.bak";
  const std::string guard = c.source + ".bookmark-edit.guard";
  if (Storage.lookupPath(staged.c_str()) != HalStorage::PathStatus::Missing) {
    c.result = Result::Conflict;
    return true;
  }
  const auto backupStatus = Storage.lookupPath(backup.c_str());
  const auto guardStatus = Storage.lookupPath(guard.c_str());
  if (backupStatus == HalStorage::PathStatus::Failed || guardStatus == HalStorage::PathStatus::Failed) return true;
  if (backupStatus == HalStorage::PathStatus::Missing && guardStatus == HalStorage::PathStatus::Present) {
    std::string pendingBackup, pendingGuard;
    for (unsigned slot = 0; slot < 64; ++slot) {
      const std::string oldBackup = backup + "-" + std::to_string(slot);
      const std::string oldGuard = guard + "-" + std::to_string(slot);
      const auto oldBackupStatus = Storage.lookupPath(oldBackup.c_str());
      const auto oldGuardStatus = Storage.lookupPath(oldGuard.c_str());
      if (oldBackupStatus == HalStorage::PathStatus::Failed || oldGuardStatus == HalStorage::PathStatus::Failed)
        return true;
      if (oldBackupStatus != HalStorage::PathStatus::Present || oldGuardStatus != HalStorage::PathStatus::Missing)
        continue;
      if (!pendingBackup.empty()) {
        c.result = Result::Conflict;
        return true;
      }
      pendingBackup = oldBackup;
      pendingGuard = oldGuard;
    }
    if (pendingBackup.empty() || !verifyPublished(c, pendingBackup, guard)) {
      c.result = Result::Conflict;
      return true;
    }
    if (Storage.lookupPath(pendingGuard.c_str()) != HalStorage::PathStatus::Missing ||
        !Storage.rename(guard.c_str(), pendingGuard.c_str())) {
      c.result = Result::Uncertain;
      return true;
    }
    c.result = Storage.lookupPath(guard.c_str()) == HalStorage::PathStatus::Missing &&
                       verifyPublished(c, pendingBackup, pendingGuard)
                   ? Result::Archived
                   : Result::Uncertain;
    return true;
  }
  if (!verifyPublished(c, backup, guard)) {
    c.result = Result::Conflict;
    return true;
  }
  for (unsigned slot = 0; slot < 64; ++slot) {
    const std::string archivedBackup = backup + "-" + std::to_string(slot);
    const std::string archivedGuard = guard + "-" + std::to_string(slot);
    const auto backupStatus = Storage.lookupPath(archivedBackup.c_str());
    const auto guardStatus = Storage.lookupPath(archivedGuard.c_str());
    if (backupStatus == HalStorage::PathStatus::Failed || guardStatus == HalStorage::PathStatus::Failed) return true;
    if (backupStatus != HalStorage::PathStatus::Missing || guardStatus != HalStorage::PathStatus::Missing) continue;
    if (!Storage.rename(backup.c_str(), archivedBackup.c_str()) ||
        Storage.lookupPath(backup.c_str()) != HalStorage::PathStatus::Missing ||
        !verifyPublished(c, archivedBackup, guard) ||
        Storage.lookupPath(archivedGuard.c_str()) != HalStorage::PathStatus::Missing ||
        !Storage.rename(guard.c_str(), archivedGuard.c_str())) {
      c.result = Result::Uncertain;
      return true;
    }
    c.result = Storage.lookupPath(guard.c_str()) == HalStorage::PathStatus::Missing &&
                       Storage.lookupPath(staged.c_str()) == HalStorage::PathStatus::Missing &&
                       verifyPublished(c, archivedBackup, archivedGuard)
                   ? Result::Archived
                   : Result::Uncertain;
    return true;
  }
  c.result = Result::Conflict;
  return true;
}

bool operate(void* raw) {
  auto& c = *static_cast<Context*>(raw);
  const std::string staged = c.source + ".bookmark-edit.tmp";
  const std::string backup = c.source + ".bookmark-edit.bak";
  const std::string guard = c.source + ".bookmark-edit.guard";
  const auto sourceStatus = Storage.lookupPath(c.source.c_str());
  const auto stageStatus = Storage.lookupPath(staged.c_str());
  const auto backupStatus = Storage.lookupPath(backup.c_str());
  const auto guardStatus = Storage.lookupPath(guard.c_str());
  if (sourceStatus == HalStorage::PathStatus::Failed || stageStatus == HalStorage::PathStatus::Failed ||
      backupStatus == HalStorage::PathStatus::Failed || guardStatus == HalStorage::PathStatus::Failed)
    return true;
  if (sourceStatus != HalStorage::PathStatus::Present || backupStatus != HalStorage::PathStatus::Missing ||
      guardStatus != HalStorage::PathStatus::Missing ||
      (c.publish ? stageStatus != HalStorage::PathStatus::Present : stageStatus != HalStorage::PathStatus::Missing)) {
    c.result = Result::Conflict;
    return true;
  }
  HalFile input = Storage.openChecked(c.source.c_str(), O_RDONLY);
  if (!input || input.isDirectory()) return true;
  HalFile output = Storage.openChecked(staged.c_str(), c.publish ? O_RDONLY : O_RDWR | O_CREAT | O_EXCL);
  if (!output || output.isDirectory()) {
    input.close();
    return true;
  }
  if (!Storage.distinctMoveChains(c.source.c_str(), staged.c_str())) {
    input.close();
    output.close();
    c.result = Result::Conflict;
    return true;
  }
  bool complete = false;
  auto result = c.publish ? codec::compareBookmarkRemoval(input, output, c.bookPath, c.first, c.count, complete)
                          : codec::writeBookmarkRemoval(input, output, c.bookPath, c.first, c.count);
  if (!c.publish && result == codec::BookmarkRewriteResult::Ready && output.sync())
    result = codec::compareBookmarkRemoval(input, output, c.bookPath, c.first, c.count, complete);
  const bool outputClosed = output.close();
  const bool inputClosed = input.close();
  if (result == codec::BookmarkRewriteResult::Invalid) {
    c.result = Result::Invalid;
    return true;
  }
  if (result != codec::BookmarkRewriteResult::Ready || !complete || !outputClosed || !inputClosed ||
      !Storage.distinctMoveChains(c.source.c_str(), staged.c_str())) {
    c.result = Result::Uncertain;
    return true;
  }
  if (!c.publish) {
    c.result = Result::Ready;
    return true;
  }
  if (!createGuard(c.source, staged, guard)) {
    c.result = Result::Uncertain;
    return true;
  }
  // Keep the old file even after successful publication; recovery owns it.
  if (!Storage.rename(c.source.c_str(), backup.c_str()) ||
      Storage.lookupPath(c.source.c_str()) != HalStorage::PathStatus::Missing ||
      Storage.lookupPath(backup.c_str()) != HalStorage::PathStatus::Present ||
      !Storage.distinctMoveChains(backup.c_str(), staged.c_str()) ||
      !Storage.distinctMoveChains(backup.c_str(), guard.c_str()) || !Storage.rename(staged.c_str(), c.source.c_str())) {
    c.result = Result::Uncertain;
    return true;
  }
  if (Storage.lookupPath(staged.c_str()) != HalStorage::PathStatus::Missing ||
      Storage.lookupPath(c.source.c_str()) != HalStorage::PathStatus::Present ||
      !Storage.distinctMoveChains(backup.c_str(), c.source.c_str())) {
    c.result = Result::Uncertain;
    return true;
  }
  input = Storage.openChecked(backup.c_str(), O_RDONLY);
  output = Storage.openChecked(c.source.c_str(), O_RDONLY);
  complete = false;
  result = input && output ? codec::compareBookmarkRemoval(input, output, c.bookPath, c.first, c.count, complete)
                           : codec::BookmarkRewriteResult::Unavailable;
  const bool finalOutputClosed = output.close();
  const bool finalInputClosed = input.close();
  c.result = result == codec::BookmarkRewriteResult::Ready && complete && finalOutputClosed && finalInputClosed
                 ? Result::Published
                 : Result::Uncertain;
  return true;
}

struct RestoreContext {
  const std::string& source;
  const std::string& bookPath;
  Result result = Result::Unavailable;
};

codec::BookmarkRewriteResult validateOriginal(const std::string& path, const std::string& bookPath) {
  HalFile file = Storage.openChecked(path.c_str(), O_RDONLY);
  if (!file || file.isDirectory()) return codec::BookmarkRewriteResult::Unavailable;
  codec::BookmarkRewrite rewrite;
  const auto result = codec::inspectBookmarkRewrite(file, bookPath, bookPath, rewrite);
  return file.close() ? result : codec::BookmarkRewriteResult::Unavailable;
}

bool restore(void* raw) {
  auto& c = *static_cast<RestoreContext*>(raw);
  const std::string staged = c.source + ".bookmark-edit.tmp";
  const std::string backup = c.source + ".bookmark-edit.bak";
  const std::string guard = c.source + ".bookmark-edit.guard";
  const auto primaryStatus = Storage.lookupPath(c.source.c_str());
  const auto backupStatus = Storage.lookupPath(backup.c_str());
  const auto stageStatus = Storage.lookupPath(staged.c_str());
  const auto guardStatus = Storage.lookupPath(guard.c_str());
  if (primaryStatus == HalStorage::PathStatus::Failed || backupStatus == HalStorage::PathStatus::Failed ||
      stageStatus == HalStorage::PathStatus::Failed || guardStatus == HalStorage::PathStatus::Failed)
    return true;
  if (primaryStatus != HalStorage::PathStatus::Missing || backupStatus != HalStorage::PathStatus::Present) {
    c.result = Result::Conflict;
    return true;
  }
  const auto validation = validateOriginal(backup, c.bookPath);
  if (validation != codec::BookmarkRewriteResult::Ready) {
    c.result = validation == codec::BookmarkRewriteResult::Invalid ? Result::Invalid : Result::Unavailable;
    return true;
  }
  if (stageStatus == HalStorage::PathStatus::Present && !Storage.distinctMoveChains(backup.c_str(), staged.c_str())) {
    c.result = Result::Conflict;
    return true;
  }
  if (guardStatus == HalStorage::PathStatus::Present &&
      (!Storage.distinctMoveChains(backup.c_str(), guard.c_str()) ||
       (stageStatus == HalStorage::PathStatus::Present &&
        !Storage.distinctMoveChains(staged.c_str(), guard.c_str())))) {
    c.result = Result::Conflict;
    return true;
  }
  if (Storage.lookupPath(c.source.c_str()) != HalStorage::PathStatus::Missing ||
      !Storage.rename(backup.c_str(), c.source.c_str())) {
    c.result = Result::Uncertain;
    return true;
  }
  c.result = Storage.lookupPath(c.source.c_str()) == HalStorage::PathStatus::Present &&
                     Storage.lookupPath(backup.c_str()) == HalStorage::PathStatus::Missing &&
                     Storage.lookupPath(staged.c_str()) == stageStatus &&
                     Storage.lookupPath(guard.c_str()) == guardStatus &&
                     (stageStatus != HalStorage::PathStatus::Present ||
                      Storage.distinctMoveChains(c.source.c_str(), staged.c_str())) &&
                     (guardStatus != HalStorage::PathStatus::Present ||
                      Storage.distinctMoveChains(c.source.c_str(), guard.c_str())) &&
                     validateOriginal(c.source, c.bookPath) == codec::BookmarkRewriteResult::Ready
                 ? Result::Restored
                 : Result::Uncertain;
  return true;
}
}  // namespace

Result stage(const std::string& source, const std::string& bookPath, uint16_t first, uint16_t count) {
  Context context{source, bookPath, first, count, false};
  return Storage.withStorageLock(operate, &context) ? context.result : Result::Unavailable;
}
Result publish(const std::string& source, const std::string& bookPath, uint16_t first, uint16_t count) {
  Context context{source, bookPath, first, count, true};
  return Storage.withStorageLock(operate, &context) ? context.result : Result::Unavailable;
}
Result archivePublished(const std::string& source, const std::string& bookPath, uint16_t first, uint16_t count) {
  Context context{source, bookPath, first, count, false};
  return Storage.withStorageLock(archive, &context) ? context.result : Result::Unavailable;
}
Result restoreOriginal(const std::string& source, const std::string& bookPath) {
  RestoreContext context{source, bookPath};
  return Storage.withStorageLock(restore, &context) ? context.result : Result::Unavailable;
}
}  // namespace legacybookmarkedit
