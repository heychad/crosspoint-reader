#include "CompletionMovePlanIO.h"

#include <CredentialIntegrity.h>
#include <HalStorage.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>

#include "CompletionJournalIO.h"
#include "CompletionLibraryDirty.h"
#include "CrossInkBookmarkMove.h"
#include "StatsSessionLease.h"

namespace readingstats::completionmove {
namespace {
namespace cj = completionjournal;
using Seal = std::array<uint8_t, 16>;

void pathFor(unsigned slot, const char* prefix, char (&path)[64]) {
  snprintf(path, sizeof(path), "/.completion-journal/%s%02u", prefix, slot);
}

bool sourceMatches(std::string_view from, const cj::Record& record, size_t size) {
  std::string_view directory;
  const uint8_t *book = nullptr, *global = nullptr;
  if (!cj::decode(record.data(), size, directory, book, global) || !(record[9] & cj::MOVE_TO_READ)) return false;
  std::string_view boundSource;
  uint64_t originHash = 0;
  if ((record[9] & cj::MOVE_FROM_DEFERRED) &&
      (!cj::decodeDeferredMove(record.data(), size, boundSource, originHash) || boundSource != from))
    return false;
  const std::string cache =
      "/.crosspoint/reading_stats/epub_" + std::to_string(std::hash<std::string>{}(std::string(from)));
  return directory == cache;
}

Seal makeSeal(unsigned slot, const Plan& plan, size_t size) {
  Seal result{
      'C', 'P', 'M', 'S', 1, static_cast<uint8_t>(slot), static_cast<uint8_t>(size), static_cast<uint8_t>(size >> 8)};
  const auto hash = cj::checksum(plan.data(), size);
  for (size_t i = 0; i < 8; ++i) result[8 + i] = static_cast<uint8_t>(hash >> (8 * i));
  return result;
}

enum class ReadResult { Read, Missing, Invalid, Failed };
ReadResult readFile(const char* path, uint8_t* out, size_t capacity, size_t& size) {
  const auto presence = Storage.lookupPath(path);
  if (presence == HalStorage::PathStatus::Missing) return ReadResult::Missing;
  if (presence != HalStorage::PathStatus::Present) return ReadResult::Failed;
  HalFile file;
  if (!Storage.openFileForRead("STATS", path, file)) return ReadResult::Failed;
  const size_t length = file.fileSize();
  if (length > capacity) return file.close() ? ReadResult::Invalid : ReadResult::Failed;
  const bool read = file.read(out, length) == static_cast<int>(length);
  const bool closed = file.close();
  if (!read || !closed) return ReadResult::Failed;
  size = length;
  return ReadResult::Read;
}

bool createFile(const char* path, const uint8_t* bytes, size_t size) {
  HalFile file = Storage.openChecked(path, O_WRONLY | O_CREAT | O_EXCL);
  if (!file) return false;
  const bool written = file.write(bytes, size) == size;
  const bool synced = file.sync();
  const bool closed = file.close();
  if (!written || !synced || !closed) return false;
  HalFile check;
  if (!Storage.openFileForRead("STATS", path, check)) return false;
  bool same = check.fileSize() == size;
  uint8_t buffer[64];
  for (size_t offset = 0; same && offset < size;) {
    const auto count = std::min(sizeof(buffer), size - offset);
    same = check.read(buffer, count) == static_cast<int>(count) && std::memcmp(buffer, bytes + offset, count) == 0;
    offset += count;
  }
  return check.close() && same;
}

struct Context {
  std::string_view requestedFrom;
  std::string_view requestedTo;
  Plan* output;
  size_t* outputSize;
  std::string_view* from;
  std::string_view* to;
  FileIdentity* identities;
  bool prepare;
  PlanResult result = PlanResult::Unavailable;
};

bool handleLocked(void* raw) {
  auto& context = *static_cast<Context*>(raw);
  if (context.prepare && StatsSessionLease::anyActive()) return true;
  auto record = makeUniqueNoThrow<cj::Record>();
  auto plan = makeUniqueNoThrow<Plan>();
  if (!record || !plan) return true;
  size_t recordSize = 0;
  unsigned slot = 0;
  const auto state = cj::scanRecords(*record, recordSize, slot);
  if (state == cj::ScanResult::Empty || state == cj::ScanResult::PreparedOnly) {
    context.result = PlanResult::Missing;
    return true;
  }
  if (state == cj::ScanResult::Conflict || state == cj::ScanResult::Damaged) {
    context.result = PlanResult::Damaged;
    return true;
  }
  if (state != cj::ScanResult::Candidate) return true;
  const auto completionHash = cj::checksum(record->data(), recordSize);
  char planPath[64], sealPath[64];
  pathFor(slot, ".cmove-", planPath);
  pathFor(slot, ".cmoveseal-", sealPath);
  size_t planSize = 0;
  const auto readPlan = readFile(planPath, plan->data(), plan->size(), planSize);
  if (readPlan == ReadResult::Failed) return true;
  if (readPlan == ReadResult::Invalid) {
    context.result = PlanResult::Damaged;
    return true;
  }
  if (readPlan == ReadResult::Missing && context.prepare) {
    if (!sourceMatches(context.requestedFrom, *record, recordSize)) {
      context.result = PlanResult::Invalid;
      return true;
    }
    // No pre-existing destination or orphan seal can be adopted.
    const std::string source(context.requestedFrom), destination(context.requestedTo);
    if (Storage.lookupPath(source.c_str()) != HalStorage::PathStatus::Present ||
        Storage.lookupPath(destination.c_str()) != HalStorage::PathStatus::Missing ||
        Storage.lookupPath(sealPath) != HalStorage::PathStatus::Missing) {
      context.result = PlanResult::Conflict;
      return true;
    }
    const auto cacheRoot = [](const std::string& book, bool legacy, bool stats) {
      const auto hash = legacy ? cj::checksum(reinterpret_cast<const uint8_t*>(book.data()), book.size())
                               : static_cast<uint64_t>(std::hash<std::string>{}(book));
      return std::string(stats ? "/.crosspoint/reading_stats/epub_" : "/.crosspoint/epub_") + std::to_string(hash);
    };
    const bool sourceKeysMatch = cacheRoot(source, true, false) == cacheRoot(source, false, false);
    const bool destinationKeysMatch = cacheRoot(destination, true, false) == cacheRoot(destination, false, false);
    if (destinationKeysMatch && !sourceKeysMatch) {
      context.result = PlanResult::Conflict;
      return true;
    }
    for (const bool stats : {false, true}) {
      const auto newCurrent = cacheRoot(destination, false, stats);
      const auto newLegacy = cacheRoot(destination, true, stats);
      // Existing destination caches may belong to another book.
      for (const auto& path : {newCurrent, newLegacy}) {
        if (path.empty()) continue;
        const auto presence = Storage.lookupPath(path.c_str());
        if (presence == HalStorage::PathStatus::Failed) return true;
        if (presence == HalStorage::PathStatus::Present) {
          context.result = PlanResult::Conflict;
          return true;
        }
      }
    }
    // Recovery artifacts are not interpreted as authoritative sources.
    const auto crc = credential_integrity::crc32(source);
    const auto hash = std::hash<std::string>{}(source);
    for (const auto key : {static_cast<size_t>(crc), hash}) {
      const auto base = "/.crosspoint/bookmarks/epub_" + std::to_string(key) + ".bin";
      for (const char* suffix : {".tmp", ".bak", ".bookmark-edit.bak", ".bookmark-edit.guard"}) {
        const auto presence = Storage.lookupPath((base + suffix).c_str());
        if (presence == HalStorage::PathStatus::Failed) return true;
        if (presence == HalStorage::PathStatus::Present) {
          context.result = PlanResult::Conflict;
          return true;
        }
      }
    }
    const auto newCrc = credential_integrity::crc32(destination);
    const auto newHash = std::hash<std::string>{}(destination);
    for (const auto key : {static_cast<size_t>(newCrc), newHash}) {
      const auto base = "/.crosspoint/bookmarks/epub_" + std::to_string(key) + ".bin";
      for (const char* suffix : {".tmp", ".bak", ".completion-copy", ".bookmark-edit.bak", ".bookmark-edit.guard"}) {
        const auto presence = Storage.lookupPath((base + suffix).c_str());
        if (presence == HalStorage::PathStatus::Failed) return true;
        if (presence == HalStorage::PathStatus::Present) {
          context.result = PlanResult::Conflict;
          return true;
        }
      }
    }
    auto identities = makeUniqueNoThrow<std::array<FileIdentity, FILE_COUNT>>();
    if (!identities) return true;
    std::string firstLegacySource, firstLegacyTarget, secondLegacySource, secondLegacyTarget;
    if (!itemPaths(source, destination, STATE_FILE_END, firstLegacySource, firstLegacyTarget) ||
        !itemPaths(source, destination, STATE_FILE_END + 1, secondLegacySource, secondLegacyTarget) ||
        firstLegacySource == secondLegacySource || firstLegacyTarget == secondLegacyTarget ||
        firstLegacySource == secondLegacyTarget || secondLegacySource == firstLegacyTarget) {
      context.result = PlanResult::Conflict;
      return true;
    }
    for (size_t i = 0; i < FILE_COUNT; ++i) {
      if (i >= LEGACY_BOOKMARK_END && sourceKeysMatch) continue;
      std::string oldPath, newPath;
      if (!itemPaths(context.requestedFrom, context.requestedTo, i, oldPath, newPath) || oldPath == newPath) {
        context.result = PlanResult::Conflict;
        return true;
      }
      FileIdentity destinationIdentity;
      const auto oldStatus = captureFile(oldPath, (*identities)[i]);
      const auto newStatus = captureFile(newPath, destinationIdentity);
      if (oldStatus == FileResult::Unavailable || newStatus == FileResult::Unavailable) return true;
      if ((oldStatus != FileResult::Ready && oldStatus != FileResult::Absent) || newStatus != FileResult::Absent ||
          (i == 0 && oldStatus != FileResult::Ready)) {
        context.result = PlanResult::Conflict;
        return true;
      }
      if (i >= STATE_FILE_END && i < LEGACY_BOOKMARK_END && oldStatus == FileResult::Ready) {
        HalFile file;
        if (!Storage.openFileForRead("BKM", oldPath, file)) return true;
        BookmarkRewrite rewrite;
        const auto valid = inspectBookmarkRewrite(file, source, destination, rewrite);
        const bool closed = file.close();
        if (!closed || valid == BookmarkRewriteResult::Unavailable) return true;
        if (valid != BookmarkRewriteResult::Ready) {
          context.result = PlanResult::Conflict;
          return true;
        }
      }
    }
    if (!encode(context.requestedFrom, context.requestedTo, completionHash, *plan, planSize, identities->data())) {
      context.result = PlanResult::Invalid;
      return true;
    }
    if (!createFile(planPath, plan->data(), planSize)) {
      context.result = PlanResult::Uncertain;
      return true;
    }
    const Seal seal = makeSeal(slot, *plan, planSize);
    if (!createFile(sealPath, seal.data(), seal.size())) {
      context.result = PlanResult::Uncertain;
      return true;
    }
  } else if (readPlan == ReadResult::Missing) {
    const auto sealPresence = Storage.lookupPath(sealPath);
    if (sealPresence == HalStorage::PathStatus::Missing)
      context.result = PlanResult::Missing;
    else if (sealPresence == HalStorage::PathStatus::Present)
      context.result = PlanResult::Damaged;
    return true;
  }
  std::string_view source, destination;
  if (!decode(plan->data(), planSize, completionHash, source, destination) ||
      !sourceMatches(source, *record, recordSize)) {
    context.result = PlanResult::Damaged;
    return true;
  }
  Seal seal{};
  size_t sealSize = 0;
  const auto readSeal = readFile(sealPath, seal.data(), seal.size(), sealSize);
  if (readSeal == ReadResult::Failed) return true;
  if (readSeal != ReadResult::Read || sealSize != seal.size() || seal != makeSeal(slot, *plan, planSize)) {
    context.result = PlanResult::Damaged;
    return true;
  }
  if (context.prepare && (source != context.requestedFrom || destination != context.requestedTo)) {
    context.result = PlanResult::Conflict;
    return true;
  }
  if (context.output) {
    *context.output = *plan;
    *context.outputSize = planSize;
    // Decode again so returned views refer to caller-owned storage.
    if (!decode(context.output->data(), planSize, completionHash, *context.from, *context.to, context.identities))
      return true;
  }
  context.result = PlanResult::Ready;
  return true;
}

struct RetiredPlanContext {
  std::string_view source;
  std::string_view destination;
  uint64_t originHash;
  uint64_t moveHash;
  Plan* output;
  size_t* outputSize;
  RetiredPlanResult result = RetiredPlanResult::Unavailable;
};

bool inspectRetiredPlanLocked(void* raw) {
  auto& context = *static_cast<RetiredPlanContext*>(raw);
  if (StatsSessionLease::anyActive()) {
    context.result = RetiredPlanResult::Busy;
    return true;
  }
  cj::RetiredJournalLocation location{};
  switch (cj::retiredDeferredMoveProof(context.source, context.originHash, context.moveHash, &location)) {
    case cj::DeferredRetirementProof::Missing:
      context.result = RetiredPlanResult::Missing;
      return true;
    case cj::DeferredRetirementProof::Busy:
      context.result = RetiredPlanResult::Busy;
      return true;
    case cj::DeferredRetirementProof::Conflict:
      context.result = RetiredPlanResult::Conflict;
      return true;
    case cj::DeferredRetirementProof::Damaged:
      context.result = RetiredPlanResult::Damaged;
      return true;
    case cj::DeferredRetirementProof::Unavailable:
      return true;
    case cj::DeferredRetirementProof::Retired:
      break;
  }
  auto plan = makeUniqueNoThrow<Plan>();
  if (!plan) return true;
  char path[64];
  snprintf(path, sizeof(path), "%s/.cmove-%02u", location.directory.data(), location.slot);
  size_t planSize = 0;
  const auto planRead = readFile(path, plan->data(), plan->size(), planSize);
  if (planRead == ReadResult::Failed) return true;
  if (planRead != ReadResult::Read) {
    context.result = RetiredPlanResult::Damaged;
    return true;
  }
  std::string_view source, destination;
  if (!decode(plan->data(), planSize, context.moveHash, source, destination)) {
    context.result = RetiredPlanResult::Damaged;
    return true;
  }
  snprintf(path, sizeof(path), "%s/.cmoveseal-%02u", location.directory.data(), location.slot);
  Seal seal{};
  size_t sealSize = 0;
  const auto sealRead = readFile(path, seal.data(), seal.size(), sealSize);
  if (sealRead == ReadResult::Failed) return true;
  if (sealRead != ReadResult::Read || sealSize != seal.size() || seal != makeSeal(location.slot, *plan, planSize)) {
    context.result = RetiredPlanResult::Damaged;
    return true;
  }
  if (source != context.source || destination != context.destination) {
    context.result = RetiredPlanResult::Conflict;
    return true;
  }
  if (context.output) {
    *context.output = *plan;
    *context.outputSize = planSize;
  }
  context.result = RetiredPlanResult::Ready;
  return true;
}
}  // namespace

RetiredPlanResult inspectRetiredPlan(std::string_view source, std::string_view destination, uint64_t originHash,
                                     uint64_t moveHash, Plan* output, size_t* outputSize) {
  if ((output == nullptr) != (outputSize == nullptr)) return RetiredPlanResult::Conflict;
  RetiredPlanContext context{source, destination, originHash, moveHash, output, outputSize};
  return Storage.withStorageLock(inspectRetiredPlanLocked, &context) ? context.result : RetiredPlanResult::Unavailable;
}

bool itemPaths(std::string_view from, std::string_view to, size_t index, std::string& source, std::string& target) {
  if (!validPath(from) || !validPath(to) || index >= FILE_COUNT) return false;
  if (index < 3) {
    static constexpr const char* suffixes[] = {"", ".key", ".rights"};
    source = std::string(from) + suffixes[index];
    target = std::string(to) + suffixes[index];
  } else if (index < 8) {
    static constexpr const char* suffixes[] = {"", ".bak", ".tmp", ".deleted", ".migrate.bak"};
    const auto path = [](std::string_view book) {
      return std::string("/.crosspoint/clippings/epub_") + std::to_string(std::hash<std::string>{}(std::string(book))) +
             ".bin";
    };
    source = path(from) + suffixes[index - 3];
    target = path(to) + suffixes[index - 3];
  } else if (index < MOVED_FILE_COUNT) {
    static constexpr const char* suffixes[] = {"", ".tmp", ".bak"};
    const auto path = [](std::string_view book) {
      std::string name(book.substr(1));
      std::replace(name.begin(), name.end(), '/', '_');
      std::replace(name.begin(), name.end(), '\\', '_');
      const auto dot = name.find_last_of('.');
      if (dot != std::string::npos) name.erase(dot);
      return std::string("/.crosspoint/bookmarks/") + name + ".json";
    };
    source = path(from) + suffixes[index - 8];
    target = path(to) + suffixes[index - 8];
  } else if (index < STATE_FILE_END || index >= LEGACY_BOOKMARK_END) {
    // Generated chapter/image cache can be rebuilt. These files contain
    // persistent reader state and must survive the path change.
    static constexpr const char* cacheFiles[] = {
        "progress.bin",        "progress.bin.tmp",        "links.bin",
        "reader_prefs.json",   "reader_prefs.json.tmp",   "reader_prefs.json.bak",
        "reader_settings.bin", "reader_settings.bin.tmp", "reader_settings.bin.bak",
        "reading_stats_off"};
    static constexpr const char* statsFiles[] = {
        "stats_v5.bin",      "stats_v5.bin.tmp",  "stats_v5.bin.bak",      "stats_v4.bin",         "stats.bin",
        "reading_stats_off", "reader_prefs.json", "reader_prefs.json.tmp", "reader_prefs.json.bak"};
    const bool isLegacy = index >= LEGACY_BOOKMARK_END;
    const size_t stateIndex = isLegacy ? index - LEGACY_BOOKMARK_END : index - MOVED_FILE_COUNT;
    const bool isCache = stateIndex < std::size(cacheFiles);
    const char* filename = isCache ? cacheFiles[stateIndex] : statsFiles[stateIndex - std::size(cacheFiles)];
    const auto path = [isCache, isLegacy, filename](std::string_view book) {
      const uint64_t hash = isLegacy ? cj::checksum(reinterpret_cast<const uint8_t*>(book.data()), book.size())
                                     : static_cast<uint64_t>(std::hash<std::string>{}(std::string(book)));
      const auto key = "epub_" + std::to_string(hash);
      return std::string(isCache ? "/.crosspoint/" : "/.crosspoint/reading_stats/") + key + "/" + filename;
    };
    source = path(from);
    target = path(to);
  } else {
    const auto path = [index](std::string_view book) {
      const size_t key =
          index == STATE_FILE_END ? credential_integrity::crc32(book) : std::hash<std::string>{}(std::string(book));
      return std::string("/.crosspoint/bookmarks/epub_") + std::to_string(key) + ".bin";
    };
    source = path(from);
    target = path(to);
  }
  return true;
}

PlanResult preparePlan(std::string_view from, std::string_view to) {
  Context context{from, to, nullptr, nullptr, nullptr, nullptr, nullptr, true};
  return Storage.withStorageLock(handleLocked, &context) ? context.result : PlanResult::Unavailable;
}

PlanResult inspectPlan(Plan& output, size_t& size, std::string_view& from, std::string_view& to,
                       FileIdentity* identities) {
  Context context{{}, {}, &output, &size, &from, &to, identities, false};
  return Storage.withStorageLock(handleLocked, &context) ? context.result : PlanResult::Unavailable;
}

namespace {
FileResult copyLegacyBookmark(const std::string& source, const std::string& target, std::string_view oldBook,
                              std::string_view newBook, const FileIdentity& expected) {
  FileIdentity actual, destination, partial;
  const std::string stagingPath = target + ".completion-copy";
  const auto a = captureFile(source, actual), b = captureFile(target, destination),
             c = captureFile(stagingPath, partial);
  if (a == FileResult::Unavailable || b == FileResult::Unavailable || c == FileResult::Unavailable)
    return FileResult::Unavailable;
  if ((a != FileResult::Ready && a != FileResult::Absent) || (b != FileResult::Ready && b != FileResult::Absent) ||
      (c != FileResult::Ready && c != FileResult::Absent))
    return FileResult::Conflict;
  if (!expected.present)
    return !actual.present && !destination.present && !partial.present ? FileResult::Absent : FileResult::Conflict;
  if (!actual.present || actual.size != expected.size || actual.checksum != expected.checksum)
    return FileResult::Conflict;
  if (destination.present && partial.present) return FileResult::Conflict;
  if (destination.present && !Storage.distinctMoveChains(source.c_str(), target.c_str())) return FileResult::Conflict;
  if (partial.present && !Storage.distinctMoveChains(source.c_str(), stagingPath.c_str())) return FileResult::Conflict;
  HalFile input;
  if (!Storage.openFileForRead("BKM", source, input)) return FileResult::Unavailable;
  if (destination.present) {
    HalFile output;
    if (!Storage.openFileForRead("BKM", target, output)) {
      input.close();
      return FileResult::Unavailable;
    }
    bool complete = false;
    const auto compared = compareBookmarkRewrite(input, output, oldBook, newBook, complete);
    const bool outputClosed = output.close(), inputClosed = input.close();
    if (!outputClosed || !inputClosed || compared == BookmarkRewriteResult::Unavailable) return FileResult::Unavailable;
    return compared == BookmarkRewriteResult::Ready && complete ? FileResult::AlreadyCopied : FileResult::Conflict;
  }
  HalFile output = Storage.openChecked(stagingPath.c_str(), partial.present ? O_RDWR : (O_RDWR | O_CREAT | O_EXCL));
  if (!output) {
    input.close();
    return FileResult::Uncertain;
  }
  const auto written = writeBookmarkRewrite(input, output, oldBook, newBook);
  const bool synced = output.sync();
  bool complete = false;
  const auto compared = written == BookmarkRewriteResult::Ready && synced
                            ? compareBookmarkRewrite(input, output, oldBook, newBook, complete)
                            : BookmarkRewriteResult::Unavailable;
  const bool outputClosed = output.close(), inputClosed = input.close();
  if (written == BookmarkRewriteResult::Invalid || compared == BookmarkRewriteResult::Invalid)
    return FileResult::Conflict;
  if (!synced || !outputClosed || !inputClosed || compared != BookmarkRewriteResult::Ready || !complete)
    return FileResult::Uncertain;
  if (!Storage.distinctMoveChains(source.c_str(), stagingPath.c_str())) return FileResult::Conflict;
  if (Storage.lookupPath(target.c_str()) != HalStorage::PathStatus::Missing ||
      !Storage.rename(stagingPath.c_str(), target.c_str()))
    return FileResult::Uncertain;
  // Re-read after publication before treating this file as complete.
  HalFile checkInput, checkTarget;
  if (!Storage.openFileForRead("BKM", source, checkInput)) return FileResult::Uncertain;
  if (!Storage.openFileForRead("BKM", target, checkTarget)) {
    checkInput.close();
    return FileResult::Uncertain;
  }
  complete = false;
  const auto verified = compareBookmarkRewrite(checkInput, checkTarget, oldBook, newBook, complete);
  const bool targetClosed = checkTarget.close(), sourceClosed = checkInput.close();
  return targetClosed && sourceClosed && verified == BookmarkRewriteResult::Ready && complete &&
                 Storage.distinctMoveChains(source.c_str(), target.c_str())
             ? FileResult::Copied
             : FileResult::Uncertain;
}

bool moveFilesLocked(void* raw) {
  auto& result = *static_cast<MoveFilesResult*>(raw);
  if (StatsSessionLease::anyActive()) return true;
  auto plan = makeUniqueNoThrow<Plan>();
  if (!plan) return true;
  size_t size = 0;
  std::string_view from, to;
  auto identities = makeUniqueNoThrow<std::array<FileIdentity, FILE_COUNT>>();
  if (!identities) return true;
  const auto state = inspectPlan(*plan, size, from, to, identities->data());
  if (state == PlanResult::Missing) {
    result = MoveFilesResult::NoPlan;
    return true;
  }
  if (state == PlanResult::Unavailable) return true;
  if (state != PlanResult::Ready) {
    result = MoveFilesResult::RecoveryRequired;
    return true;
  }
  if (!(*identities)[0].present) {
    result = MoveFilesResult::RecoveryRequired;
    return true;
  }
  cj::SnapshotState bookState, globalState;
  if (cj::checkReplay(bookState, globalState) != cj::ReplayCheck::Ready || bookState != cj::SnapshotState::After ||
      globalState != cj::SnapshotState::After) {
    result = MoveFilesResult::RecoveryRequired;
    return true;
  }
  if (!markLibraryDirtyForMove()) {
    result = MoveFilesResult::RecoveryRequired;
    return true;
  }
  // Keep the original stats available to the completion journal until the
  // transaction is finalized; copy state before moving the EPUB.
  for (size_t i = MOVED_FILE_COUNT; i < FILE_COUNT; ++i) {
    if (i >= STATE_FILE_END && i < LEGACY_BOOKMARK_END) continue;
    if (i >= LEGACY_BOOKMARK_END && cj::checksum(reinterpret_cast<const uint8_t*>(from.data()), from.size()) ==
                                        static_cast<uint64_t>(std::hash<std::string>{}(std::string(from))))
      continue;
    // The legacy progress fallback below owns the native destination when the
    // source has no native progress. Avoid treating that copy as a conflict.
    if (i == MOVED_FILE_COUNT && !(*identities)[i].present && (*identities)[LEGACY_BOOKMARK_END].present) continue;
    std::string oldPath, newPath;
    if (!itemPaths(from, to, i, oldPath, newPath)) {
      result = MoveFilesResult::Conflict;
      return true;
    }
    if ((*identities)[i].present) {
      const auto slash = newPath.find_last_of('/');
      const auto directory = newPath.substr(0, slash);
      const auto presence = Storage.lookupPath(directory.c_str());
      if (presence == HalStorage::PathStatus::Failed ||
          (presence == HalStorage::PathStatus::Missing && !Storage.mkdir(directory.c_str()))) {
        result = MoveFilesResult::RecoveryRequired;
        return true;
      }
    }
    const auto copied = copyFileChecked(oldPath, newPath, (*identities)[i]);
    if (copied == FileResult::Copied || copied == FileResult::AlreadyCopied || copied == FileResult::Absent) continue;
    result = copied == FileResult::Conflict ? MoveFilesResult::Conflict : MoveFilesResult::RecoveryRequired;
    return true;
  }
  if (!(*identities)[MOVED_FILE_COUNT].present && (*identities)[LEGACY_BOOKMARK_END].present) {
    std::string legacySource, legacyTarget, currentSource, currentTarget;
    if (!itemPaths(from, to, LEGACY_BOOKMARK_END, legacySource, legacyTarget) ||
        !itemPaths(from, to, MOVED_FILE_COUNT, currentSource, currentTarget)) {
      result = MoveFilesResult::Conflict;
      return true;
    }
    const auto slash = currentTarget.find_last_of('/');
    const auto directory = currentTarget.substr(0, slash);
    const auto presence = Storage.lookupPath(directory.c_str());
    if (presence == HalStorage::PathStatus::Failed ||
        (presence == HalStorage::PathStatus::Missing && !Storage.mkdir(directory.c_str()))) {
      result = MoveFilesResult::RecoveryRequired;
      return true;
    }
    const auto copied = copyFileChecked(legacySource, currentTarget, (*identities)[LEGACY_BOOKMARK_END]);
    if (copied != FileResult::Copied && copied != FileResult::AlreadyCopied) {
      result = copied == FileResult::Conflict ? MoveFilesResult::Conflict : MoveFilesResult::RecoveryRequired;
      return true;
    }
  }
  for (size_t i = STATE_FILE_END; i < LEGACY_BOOKMARK_END; ++i) {
    std::string oldPath, newPath;
    if (!itemPaths(from, to, i, oldPath, newPath)) {
      result = MoveFilesResult::Conflict;
      return true;
    }
    const auto copied = copyLegacyBookmark(oldPath, newPath, from, to, (*identities)[i]);
    if (copied == FileResult::Copied || copied == FileResult::AlreadyCopied || copied == FileResult::Absent) continue;
    result = copied == FileResult::Conflict ? MoveFilesResult::Conflict : MoveFilesResult::RecoveryRequired;
    return true;
  }
  const auto readDirectory = Storage.lookupPath("/read");
  if (readDirectory == HalStorage::PathStatus::Failed ||
      (readDirectory == HalStorage::PathStatus::Missing && !Storage.mkdir("/read"))) {
    result = MoveFilesResult::RecoveryRequired;
    return true;
  }
  for (size_t i = 0; i < MOVED_FILE_COUNT; ++i) {
    std::string oldPath, newPath;
    if (!itemPaths(from, to, i, oldPath, newPath)) {
      result = MoveFilesResult::Conflict;
      return true;
    }
    const auto moved = moveFileChecked(oldPath, newPath, (*identities)[i]);
    if (moved == FileResult::Moved || moved == FileResult::AlreadyMoved || moved == FileResult::Absent) continue;
    result = moved == FileResult::Conflict ? MoveFilesResult::Conflict : MoveFilesResult::RecoveryRequired;
    return true;
  }
  result = MoveFilesResult::Moved;
  return true;
}
// Compare existing files only; this function must never create or repair data.
VerifyFilesResult verifyFilesForPlan(std::string_view from, std::string_view to,
                                     const std::array<FileIdentity, FILE_COUNT>& identities) {
  const auto matches = [](const FileIdentity& a, const FileIdentity& b) {
    return a.present == b.present && (!a.present || (a.size == b.size && a.checksum == b.checksum));
  };
  const bool sameSourceKeys = cj::checksum(reinterpret_cast<const uint8_t*>(from.data()), from.size()) ==
                              static_cast<uint64_t>(std::hash<std::string>{}(std::string(from)));
  for (size_t i = 0; i < FILE_COUNT; ++i) {
    if (i >= LEGACY_BOOKMARK_END && sameSourceKeys) continue;
    std::string oldPath, newPath;
    if (!itemPaths(from, to, i, oldPath, newPath)) {
      return VerifyFilesResult::NotReady;
    }
    const auto& expected = identities[i];
    if (i < MOVED_FILE_COUNT) {
      const auto status = inspectMoveFile(oldPath, newPath, expected);
      if (status == MoveFileState::Unavailable) return VerifyFilesResult::Unavailable;
      if (status != (expected.present ? MoveFileState::AtDestination : MoveFileState::Absent)) {
        return VerifyFilesResult::NotReady;
      }
      continue;
    }
    FileIdentity source, target, staging;
    const auto a = captureFile(oldPath, source), b = captureFile(newPath, target);
    const auto c = captureFile(newPath + ".completion-copy", staging);
    if (a == FileResult::Unavailable || b == FileResult::Unavailable || c == FileResult::Unavailable)
      return VerifyFilesResult::Unavailable;
    if ((a != FileResult::Ready && a != FileResult::Absent) || (b != FileResult::Ready && b != FileResult::Absent) ||
        c != FileResult::Absent || !matches(source, expected)) {
      return VerifyFilesResult::NotReady;
    }
    if (i == MOVED_FILE_COUNT && !expected.present && identities[LEGACY_BOOKMARK_END].present) {
      if (!matches(target, identities[LEGACY_BOOKMARK_END])) {
        return VerifyFilesResult::NotReady;
      }
      std::string legacySource, legacyTarget;
      if (!itemPaths(from, to, LEGACY_BOOKMARK_END, legacySource, legacyTarget) ||
          !Storage.distinctMoveChains(legacySource.c_str(), newPath.c_str())) {
        return VerifyFilesResult::NotReady;
      }
      continue;
    }
    if (expected.present && !Storage.distinctMoveChains(oldPath.c_str(), newPath.c_str())) {
      return VerifyFilesResult::NotReady;
    }
    if (i < STATE_FILE_END || i >= LEGACY_BOOKMARK_END) {
      if (!matches(target, expected)) {
        return VerifyFilesResult::NotReady;
      }
      continue;
    }
    if (!expected.present) {
      if (target.present) {
        return VerifyFilesResult::NotReady;
      }
      continue;
    }
    if (!target.present) {
      return VerifyFilesResult::NotReady;
    }
    HalFile input, output;
    if (!Storage.openFileForRead("BKM", oldPath, input)) return VerifyFilesResult::Unavailable;
    if (!Storage.openFileForRead("BKM", newPath, output)) {
      input.close();
      return VerifyFilesResult::Unavailable;
    }
    bool complete = false;
    const auto compared = compareBookmarkRewrite(input, output, from, to, complete);
    const bool outputClosed = output.close(), inputClosed = input.close();
    if (!outputClosed || !inputClosed || compared == BookmarkRewriteResult::Unavailable)
      return VerifyFilesResult::Unavailable;
    if (compared != BookmarkRewriteResult::Ready || !complete) {
      return VerifyFilesResult::NotReady;
    }
  }
  return VerifyFilesResult::ContentMatches;
}

bool verifyFilesLocked(void* raw) {
  auto& result = *static_cast<VerifyFilesResult*>(raw);
  if (StatsSessionLease::anyActive()) return true;
  auto plan = makeUniqueNoThrow<Plan>();
  auto identities = makeUniqueNoThrow<std::array<FileIdentity, FILE_COUNT>>();
  if (!plan || !identities) return true;
  size_t size = 0;
  std::string_view from, to;
  const auto state = inspectPlan(*plan, size, from, to, identities->data());
  if (state == PlanResult::Missing) {
    result = VerifyFilesResult::NoPlan;
    return true;
  }
  if (state == PlanResult::Unavailable) return true;
  if (state != PlanResult::Ready || !(*identities)[0].present) {
    result = VerifyFilesResult::NotReady;
    return true;
  }
  cj::SnapshotState bookState, globalState;
  if (cj::checkReplay(bookState, globalState) != cj::ReplayCheck::Ready || bookState != cj::SnapshotState::After ||
      globalState != cj::SnapshotState::After) {
    result = VerifyFilesResult::NotReady;
    return true;
  }
  result = verifyFilesForPlan(from, to, *identities);
  return true;
}

struct RetiredFilesContext {
  std::string_view source;
  std::string_view destination;
  uint64_t originHash;
  uint64_t moveHash;
  VerifyFilesResult result = VerifyFilesResult::Unavailable;
};

bool verifyRetiredFilesLocked(void* raw) {
  auto& context = *static_cast<RetiredFilesContext*>(raw);
  if (StatsSessionLease::anyActive()) return true;
  auto plan = makeUniqueNoThrow<Plan>();
  auto identities = makeUniqueNoThrow<std::array<FileIdentity, FILE_COUNT>>();
  if (!plan || !identities) return true;
  size_t size = 0;
  const auto state =
      inspectRetiredPlan(context.source, context.destination, context.originHash, context.moveHash, plan.get(), &size);
  if (state == RetiredPlanResult::Missing) {
    context.result = VerifyFilesResult::NoPlan;
    return true;
  }
  if (state == RetiredPlanResult::Unavailable || state == RetiredPlanResult::Busy) return true;
  if (state != RetiredPlanResult::Ready) {
    context.result = VerifyFilesResult::NotReady;
    return true;
  }
  std::string_view from, to;
  if (!decode(plan->data(), size, context.moveHash, from, to, identities->data()) || !(*identities)[0].present) {
    context.result = VerifyFilesResult::NotReady;
    return true;
  }
  context.result = verifyFilesForPlan(from, to, *identities);
  return true;
}
}  // namespace

MoveFilesResult movePreparedFiles() {
  auto result = MoveFilesResult::Unavailable;
  return Storage.withStorageLock(moveFilesLocked, &result) ? result : MoveFilesResult::Unavailable;
}

VerifyFilesResult verifyPreparedFiles() {
  auto result = VerifyFilesResult::Unavailable;
  return Storage.withStorageLock(verifyFilesLocked, &result) ? result : VerifyFilesResult::Unavailable;
}

VerifyFilesResult verifyRetiredFiles(std::string_view source, std::string_view destination, uint64_t originHash,
                                     uint64_t moveHash) {
  RetiredFilesContext context{source, destination, originHash, moveHash};
  return Storage.withStorageLock(verifyRetiredFilesLocked, &context) ? context.result : VerifyFilesResult::Unavailable;
}
}  // namespace readingstats::completionmove
