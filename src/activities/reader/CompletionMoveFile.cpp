#include "CompletionMoveFile.h"

#include <HalStorage.h>

#include <algorithm>
#include <cstring>

namespace readingstats::completionmove {
namespace {
FileResult captureLocked(const std::string& path, FileIdentity& output) {
  const auto state = Storage.lookupPath(path.c_str());
  if (state == HalStorage::PathStatus::Missing) {
    output = {};
    return FileResult::Absent;
  }
  if (state != HalStorage::PathStatus::Present) return FileResult::Unavailable;
  HalFile file;
  if (!Storage.openFileForRead("MOVE", path, file)) return FileResult::Unavailable;
  if (file.isDirectory()) {
    return file.close() ? FileResult::Conflict : FileResult::Unavailable;
  }
  FileIdentity identity{true, file.fileSize(), 14695981039346656037ULL};
  uint8_t buffer[256];
  uint64_t offset = 0;
  bool ok = true;
  while (offset < identity.size) {
    const size_t count = static_cast<size_t>(std::min<uint64_t>(sizeof(buffer), identity.size - offset));
    if (file.read(buffer, count) != static_cast<int>(count)) {
      ok = false;
      break;
    }
    for (size_t i = 0; i < count; ++i) identity.checksum = (identity.checksum ^ buffer[i]) * 1099511628211ULL;
    offset += count;
  }
  const bool closed = file.close();
  if (!ok || !closed) return FileResult::Unavailable;
  output = identity;
  return FileResult::Ready;
}

bool equal(const FileIdentity& left, const FileIdentity& right) {
  return left.present == right.present &&
         (!left.present || (left.size == right.size && left.checksum == right.checksum));
}

struct CaptureContext {
  const std::string& path;
  FileIdentity& output;
  FileResult result = FileResult::Unavailable;
};
bool captureCallback(void* raw) {
  auto& context = *static_cast<CaptureContext*>(raw);
  context.result = captureLocked(context.path, context.output);
  return true;
}

struct MoveContext {
  const std::string& from;
  const std::string& to;
  const FileIdentity& expected;
  FileResult result = FileResult::Unavailable;
};

struct InspectMoveContext {
  const std::string& from;
  const std::string& to;
  const FileIdentity& expected;
  MoveFileState result = MoveFileState::Unavailable;
};
bool inspectMoveCallback(void* raw) {
  auto& context = *static_cast<InspectMoveContext*>(raw);
  if (context.from.empty() || context.to.empty() || context.from == context.to) {
    context.result = MoveFileState::Conflict;
    return true;
  }
  FileIdentity source, target;
  const auto a = captureLocked(context.from, source), b = captureLocked(context.to, target);
  if (a == FileResult::Unavailable || b == FileResult::Unavailable) return true;
  if ((a != FileResult::Ready && a != FileResult::Absent) || (b != FileResult::Ready && b != FileResult::Absent)) {
    context.result = MoveFileState::Conflict;
    return true;
  }
  if (!context.expected.present) {
    context.result = !source.present && !target.present ? MoveFileState::Absent : MoveFileState::Conflict;
  } else if (source.present && target.present) {
    context.result = equal(source, context.expected) && equal(target, context.expected) ? MoveFileState::Duplicate
                                                                                        : MoveFileState::Conflict;
  } else if (source.present && equal(source, context.expected)) {
    context.result = MoveFileState::AtSource;
  } else if (target.present && equal(target, context.expected)) {
    context.result = MoveFileState::AtDestination;
  } else {
    context.result = MoveFileState::Conflict;
  }
  return true;
}
bool moveCallback(void* raw) {
  auto& context = *static_cast<MoveContext*>(raw);
  if (context.from.empty() || context.to.empty() || context.from == context.to) {
    context.result = FileResult::Conflict;
    return true;
  }
  FileIdentity source, target;
  const auto a = captureLocked(context.from, source);
  const auto b = captureLocked(context.to, target);
  if (a == FileResult::Unavailable || b == FileResult::Unavailable) return true;
  if ((a != FileResult::Ready && a != FileResult::Absent) || (b != FileResult::Ready && b != FileResult::Absent)) {
    context.result = FileResult::Conflict;
    return true;
  }
  if (!context.expected.present) {
    context.result = !source.present && !target.present ? FileResult::Absent : FileResult::Conflict;
    return true;
  }
  if (!source.present && target.present && equal(target, context.expected)) {
    context.result = FileResult::AlreadyMoved;
    return true;
  }
  if (!source.present || target.present || !equal(source, context.expected)) {
    context.result = FileResult::Conflict;
    return true;
  }
  if (!Storage.rename(context.from.c_str(), context.to.c_str())) {
    context.result = FileResult::Uncertain;
    return true;
  }
  FileIdentity verifiedSource, verifiedTarget;
  const auto sourceAfter = captureLocked(context.from, verifiedSource);
  const auto targetAfter = captureLocked(context.to, verifiedTarget);
  context.result =
      sourceAfter == FileResult::Absent && targetAfter == FileResult::Ready && equal(verifiedTarget, context.expected)
          ? FileResult::Moved
          : FileResult::Uncertain;
  return true;
}

bool copyCallback(void* raw) {
  auto& context = *static_cast<MoveContext*>(raw);
  if (context.from.empty() || context.to.empty() || context.from == context.to) {
    context.result = FileResult::Conflict;
    return true;
  }
  const std::string temporary = context.to + ".completion-copy";
  if (temporary == context.from) {
    context.result = FileResult::Conflict;
    return true;
  }
  FileIdentity source, target, staging;
  const auto a = captureLocked(context.from, source);
  const auto b = captureLocked(context.to, target);
  const auto c = captureLocked(temporary, staging);
  if (a == FileResult::Unavailable || b == FileResult::Unavailable || c == FileResult::Unavailable) return true;
  if ((a != FileResult::Ready && a != FileResult::Absent) || (b != FileResult::Ready && b != FileResult::Absent) ||
      (c != FileResult::Ready && c != FileResult::Absent)) {
    context.result = FileResult::Conflict;
    return true;
  }
  if (!context.expected.present) {
    context.result = !source.present && !target.present && !staging.present ? FileResult::Absent : FileResult::Conflict;
    return true;
  }
  if (!equal(source, context.expected)) {
    context.result = FileResult::Conflict;
    return true;
  }
  if (target.present) {
    context.result = equal(target, context.expected) && !staging.present &&
                             Storage.distinctMoveChains(context.from.c_str(), context.to.c_str())
                         ? FileResult::AlreadyCopied
                         : FileResult::Conflict;
    return true;
  }
  if (staging.present && staging.size > context.expected.size) {
    context.result = FileResult::Conflict;
    return true;
  }
  // A failed earlier rename can leave two paths to one chain. Never append to
  // such a stage or accept it as an independent copy of reader state.
  if (staging.present && !Storage.distinctMoveChains(context.from.c_str(), temporary.c_str())) {
    context.result = FileResult::Conflict;
    return true;
  }
  if (!staging.present || !equal(staging, context.expected)) {
    HalFile input;
    if (!Storage.openFileForRead("MOVE", context.from, input)) return true;
    if (staging.present) {
      HalFile partial;
      if (!Storage.openFileForRead("MOVE", temporary, partial)) {
        input.close();
        return true;
      }
      uint8_t sourceBytes[256], partialBytes[256];
      uint64_t remainingPrefix = staging.size;
      bool matches = true;
      while (remainingPrefix) {
        const size_t count = static_cast<size_t>(std::min<uint64_t>(sizeof(sourceBytes), remainingPrefix));
        if (input.read(sourceBytes, count) != static_cast<int>(count) ||
            partial.read(partialBytes, count) != static_cast<int>(count)) {
          const bool aClosed = input.close();
          const bool bClosed = partial.close();
          (void)aClosed;
          (void)bClosed;
          return true;
        }
        if (std::memcmp(sourceBytes, partialBytes, count) != 0) {
          matches = false;
          break;
        }
        remainingPrefix -= count;
      }
      if (!partial.close()) {
        input.close();
        return true;
      }
      if (!matches) {
        input.close();
        context.result = FileResult::Conflict;
        return true;
      }
    }
    HalFile output = Storage.openChecked(temporary.c_str(), staging.present ? O_WRONLY : (O_WRONLY | O_CREAT | O_EXCL));
    if (!output) {
      input.close();
      context.result = FileResult::Uncertain;
      return true;
    }
    if (staging.present && !output.seekSet(static_cast<size_t>(staging.size))) {
      output.close();
      input.close();
      context.result = FileResult::Uncertain;
      return true;
    }
    uint8_t buffer[256];
    uint64_t remaining = context.expected.size - (staging.present ? staging.size : 0);
    bool ok = true;
    while (remaining) {
      const size_t count = static_cast<size_t>(std::min<uint64_t>(sizeof(buffer), remaining));
      if (input.read(buffer, count) != static_cast<int>(count) || output.write(buffer, count) != count) {
        ok = false;
        break;
      }
      remaining -= count;
    }
    const bool synced = output.sync();
    const bool outputClosed = output.close();
    const bool inputClosed = input.close();
    if (!ok || !synced || !outputClosed || !inputClosed || captureLocked(temporary, staging) != FileResult::Ready ||
        !equal(staging, context.expected)) {
      context.result = FileResult::Uncertain;
      return true;
    }
  }
  if (!Storage.distinctMoveChains(context.from.c_str(), temporary.c_str())) {
    context.result = FileResult::Conflict;
    return true;
  }
  if (!Storage.rename(temporary.c_str(), context.to.c_str())) {
    context.result = FileResult::Uncertain;
    return true;
  }
  FileIdentity verifiedTarget, verifiedStaging;
  context.result = captureLocked(context.to, verifiedTarget) == FileResult::Ready &&
                           equal(verifiedTarget, context.expected) &&
                           captureLocked(temporary, verifiedStaging) == FileResult::Absent &&
                           Storage.distinctMoveChains(context.from.c_str(), context.to.c_str())
                       ? FileResult::Copied
                       : FileResult::Uncertain;
  return true;
}
}  // namespace

FileResult captureFile(const std::string& path, FileIdentity& identity) {
  CaptureContext context{path, identity};
  return Storage.withStorageLock(captureCallback, &context) ? context.result : FileResult::Unavailable;
}

FileResult moveFileChecked(const std::string& from, const std::string& to, const FileIdentity& identity) {
  MoveContext context{from, to, identity};
  return Storage.withStorageLock(moveCallback, &context) ? context.result : FileResult::Unavailable;
}

MoveFileState inspectMoveFile(const std::string& from, const std::string& to, const FileIdentity& identity) {
  InspectMoveContext context{from, to, identity};
  return Storage.withStorageLock(inspectMoveCallback, &context) ? context.result : MoveFileState::Unavailable;
}

FileResult copyFileChecked(const std::string& from, const std::string& to, const FileIdentity& identity) {
  MoveContext context{from, to, identity};
  return Storage.withStorageLock(copyCallback, &context) ? context.result : FileResult::Unavailable;
}
}  // namespace readingstats::completionmove
