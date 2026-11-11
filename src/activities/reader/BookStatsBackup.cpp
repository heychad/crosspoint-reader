#include "BookStatsBackup.h"

#include <Arduino.h>
#include <HalStorage.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include "BookReadingStats.h"
#include "CompletionTransaction.h"
#include "ReadingStatsStore.h"
#include "StatsAccessGate.h"
#include "StatsSessionLease.h"

namespace {
using Bytes = std::array<uint8_t, BookReadingStats::CURRENT_FILE_SIZE>;
constexpr char ROOT[] = "/.crossink-stats-backup";
constexpr char BOOKS[] = "/.crossink-stats-backup/books";

bool readMatches(const std::string& path, const Bytes& expected) {
  HalFile file;
  if (!Storage.openFileForRead("STATS", path, file)) return false;
  Bytes actual{};
  const bool matches = file.fileSize() == actual.size() &&
                       file.read(actual.data(), actual.size()) == static_cast<int>(actual.size()) && actual == expected;
  return file.close() && matches;
}

bool ensureDirectory(const std::string& path) {
  const auto status = Storage.lookupPath(path.c_str());
  if (status == HalStorage::PathStatus::Failed) return false;
  if (status == HalStorage::PathStatus::Missing) return Storage.mkdir(path.c_str());
  HalFile dir = Storage.open(path.c_str());
  if (!dir) return false;
  const bool isDirectory = dir.isDirectory();
  return dir.close() && isDirectory;
}

bool validCacheLeaf(const std::string& leaf) {
  if (leaf.empty() || leaf.size() > 64) return false;
  for (const auto c : leaf) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
      return false;
  }
  return true;
}

uint64_t hashBytes(const Bytes& bytes) {
  uint64_t hash = 14695981039346656037ULL;
  for (const auto byte : bytes) hash = (hash ^ byte) * 1099511628211ULL;
  return hash;
}

bool filenameHash(const std::string& name, uint64_t& hash) {
  if (name.size() != 36 || name.compare(0, 5, "book_") != 0 || name[15] != '_' || name.compare(32, 4, ".bin") != 0)
    return false;
  for (size_t i = 5; i < 15; ++i)
    if (name[i] < '0' || name[i] > '9') return false;
  uint64_t value = 0;
  for (size_t i = 16; i < 32; ++i) {
    const char c = name[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    value = (value << 4) | static_cast<uint64_t>(c <= '9' ? c - '0' : c - 'a' + 10);
  }
  hash = value;
  return true;
}
}  // namespace

namespace {
bool createBookBackupUnlocked(const std::string& cachePath, const BookReadingStats& stats,
                              std::string* createdPath = nullptr) {
  const size_t slash = cachePath.find_last_of('/');
  const std::string leaf = cachePath.substr(slash == std::string::npos ? 0 : slash + 1);
  if (!validCacheLeaf(leaf)) return false;
  Bytes bytes{};
  if (!stats.encode(bytes.data(), bytes.size())) return false;
  const std::string source = readingstats::bookDirectory(cachePath) + "/stats_v5.bin";
  if (Storage.lookupPath(source.c_str()) != HalStorage::PathStatus::Present || !readMatches(source, bytes))
    return false;
  const std::string directory = std::string(BOOKS) + "/" + leaf;
  if (!ensureDirectory(ROOT) || !ensureDirectory(BOOKS) || !ensureDirectory(directory)) return false;
  const uint32_t sequence = millis() + 1;
  if (!sequence) return false;
  char filename[64];
  snprintf(filename, sizeof(filename), "book_%010lu_%016llx.bin", static_cast<unsigned long>(sequence),
           static_cast<unsigned long long>(hashBytes(bytes)));
  const std::string target = directory + "/" + filename;
  const std::string previousPartial = target + ".part";
  if (Storage.lookupPath(target.c_str()) != HalStorage::PathStatus::Missing ||
      Storage.lookupPath(previousPartial.c_str()) != HalStorage::PathStatus::Missing)
    return false;
  // A failed rename can leave two directory entries sharing one cluster.
  // Write a new, exclusive target instead and preserve any incomplete file.
  HalFile file = Storage.open(target.c_str(), O_WRONLY | O_CREAT | O_EXCL);
  if (!file) return false;
  const bool written = file.write(bytes.data(), bytes.size()) == bytes.size();
  file.flush();
  if (!file.close() || !written) return false;
  if (!readMatches(target, bytes)) return false;
  if (createdPath) *createdPath = target;
  return true;
}
}  // namespace

bool statsbackup::createBookBackup(const std::string& cachePath, const BookReadingStats& stats) {
  struct Context {
    const std::string& cachePath;
    const BookReadingStats& stats;
  } context{cachePath, stats};
  const auto save = [](void* raw) {
    const auto& input = *static_cast<Context*>(raw);
    return readingstats::normalStatsAllowed() && createBookBackupUnlocked(input.cachePath, input.stats);
  };
  return Storage.withStorageLock(save, &context);
}

bool statsbackup::readBookBackup(const std::string& cachePath, const std::string& filename, BookReadingStats& output) {
  const size_t slash = cachePath.find_last_of('/');
  const std::string leaf = cachePath.substr(slash == std::string::npos ? 0 : slash + 1);
  uint64_t expected = 0;
  if (!validCacheLeaf(leaf) || !filenameHash(filename, expected)) return false;
  struct Context {
    const std::string& leaf;
    const std::string& filename;
    uint64_t expected;
    BookReadingStats& output;
  } context{leaf, filename, expected, output};
  const auto read = [](void* raw) {
    auto& input = *static_cast<Context*>(raw);
    if (!readingstats::normalStatsAllowed()) return false;
    const std::string path = std::string(BOOKS) + "/" + input.leaf + "/" + input.filename;
    HalFile file;
    if (Storage.lookupPath(path.c_str()) != HalStorage::PathStatus::Present ||
        !Storage.openFileForRead("STATS", path, file))
      return false;
    Bytes bytes{};
    const bool readOk =
        file.fileSize() == bytes.size() && file.read(bytes.data(), bytes.size()) == static_cast<int>(bytes.size());
    const bool closed = file.close();
    if (!readOk || !closed || hashBytes(bytes) != input.expected) return false;
    BookReadingStats decoded;
    if (!BookReadingStats::decode(bytes.data(), bytes.size(), decoded)) return false;
    input.output = decoded;
    return true;
  };
  return Storage.withStorageLock(read, &context);
}

namespace {
using statsbackup::BookRestoreResult;
enum class ReplacementSource { Backup, Empty };

BookRestoreResult replaceBookStats(const std::string& cachePath, const std::string& bookPath,
                                   const std::string& filename, ReplacementSource source,
                                   const uint8_t* expectedCurrent, size_t expectedCurrentSize,
                                   const uint8_t* expectedBackup, size_t expectedBackupSize) {
  struct Context {
    const std::string& cachePath;
    const std::string& bookPath;
    const std::string& filename;
    ReplacementSource source;
    const uint8_t* expectedCurrent;
    size_t expectedCurrentSize;
    const uint8_t* expectedBackup;
    size_t expectedBackupSize;
    BookRestoreResult result = BookRestoreResult::Unavailable;
  } context{cachePath,           bookPath,       filename,          source, expectedCurrent,
            expectedCurrentSize, expectedBackup, expectedBackupSize};
  const auto restore = [](void* raw) {
    auto& input = *static_cast<Context*>(raw);
    if (!readingstats::normalStatsAllowed()) return true;
    if (StatsSessionLease::anyActive()) {
      input.result = BookRestoreResult::Busy;
      return true;
    }
    if (!readingstats::completionCacheMatchesBook(input.bookPath, input.cachePath) ||
        Storage.lookupPath(input.bookPath.c_str()) != HalStorage::PathStatus::Present)
      return true;
    const size_t slash = input.cachePath.find_last_of('/');
    const std::string leaf = input.cachePath.substr(slash == std::string::npos ? 0 : slash + 1);
    if (!validCacheLeaf(leaf)) return true;
    const std::string directory = readingstats::bookDirectory(input.cachePath);
    const std::string primary = directory + "/stats_v5.bin";
    const std::string oldBackup = primary + ".bak";
    const bool fromBackup = input.source == ReplacementSource::Backup;
    const std::string source = fromBackup ? std::string(BOOKS) + "/" + leaf + "/" + input.filename : "";
    const auto oldBackupStatus = Storage.lookupPath(oldBackup.c_str());
    const auto primaryStatus = Storage.lookupPath(primary.c_str());
    if (!fromBackup && primaryStatus == HalStorage::PathStatus::Missing && !input.expectedCurrent &&
        !input.expectedCurrentSize) {
      bool enabled = false, found = false, known = false;
      const auto prior = readingstats::loadBook(input.cachePath, input.bookPath, enabled, &found,
                                                BookReadingStats::LoadMode::TransactionalEdit, &known);
      if (known && !found && prior.writable) input.result = BookRestoreResult::Restored;
      return true;
    }
    if (primaryStatus != HalStorage::PathStatus::Present || oldBackupStatus == HalStorage::PathStatus::Failed)
      return true;
    const BookReadingStats current = BookReadingStats::load(directory, BookReadingStats::LoadMode::TransactionalEdit);
    BookReadingStats replacement;
    Bytes currentBytes{}, replacementBytes{};
    if (!current.writable || !current.encode(currentBytes.data(), currentBytes.size()) ||
        (fromBackup && !statsbackup::readBookBackup(input.cachePath, input.filename, replacement)) ||
        !replacement.encode(replacementBytes.data(), replacementBytes.size()))
      return true;
    if ((input.expectedCurrent && (input.expectedCurrentSize != currentBytes.size() ||
                                   memcmp(input.expectedCurrent, currentBytes.data(), currentBytes.size()) != 0)) ||
        (!input.expectedCurrent && input.expectedCurrentSize) ||
        (input.expectedBackup &&
         (input.expectedBackupSize != replacementBytes.size() ||
          memcmp(input.expectedBackup, replacementBytes.data(), replacementBytes.size()) != 0)) ||
        (!input.expectedBackup && input.expectedBackupSize))
      return true;
    if (!fromBackup && currentBytes == replacementBytes) {
      input.result = BookRestoreResult::Restored;
      return true;
    }
    if (!Storage.settingsChainEligible(primary.c_str()) ||
        (fromBackup && (!Storage.settingsChainEligible(source.c_str()) ||
                        !Storage.distinctSettingsChains(primary.c_str(), source.c_str()))) ||
        (oldBackupStatus == HalStorage::PathStatus::Present &&
         (!Storage.settingsChainEligible(oldBackup.c_str()) ||
          !Storage.distinctSettingsChains(primary.c_str(), oldBackup.c_str()) ||
          (fromBackup && !Storage.distinctSettingsChains(source.c_str(), oldBackup.c_str())))))
      return true;
    std::string before;
    if (!createBookBackupUnlocked(input.cachePath, current, &before) ||
        !Storage.settingsChainEligible(before.c_str()) ||
        !Storage.distinctSettingsChains(primary.c_str(), before.c_str()) ||
        (fromBackup && !Storage.distinctSettingsChains(source.c_str(), before.c_str())) ||
        (oldBackupStatus == HalStorage::PathStatus::Present &&
         !Storage.distinctSettingsChains(oldBackup.c_str(), before.c_str()))) {
      input.result = BookRestoreResult::BackupFailed;
      return true;
    }
    if (current.isCompleted != replacement.isCompleted) {
      const auto result =
          readingstats::restoreClosedBookSnapshot(input.bookPath, input.cachePath, currentBytes, replacementBytes);
      if (result == readingstats::ClosedCompletionResult::Applied) {
        const auto finished = readingstats::finalizeClosedBookCompletion(input.bookPath, replacement.isCompleted);
        input.result = finished == readingstats::CompletionFinalizeResult::Finalized
                           ? BookRestoreResult::Restored
                           : BookRestoreResult::RecoveryRequired;
      } else if (result == readingstats::ClosedCompletionResult::Busy) {
        input.result = BookRestoreResult::Busy;
      } else if (result == readingstats::ClosedCompletionResult::RecoveryRequired) {
        input.result = BookRestoreResult::RecoveryRequired;
      }
      return true;
    }
    input.result = replacement.save(directory) ? BookRestoreResult::Restored : BookRestoreResult::WriteUncertain;
    return true;
  };
  if (!Storage.withStorageLock(restore, &context)) return BookRestoreResult::Unavailable;
  return context.result;
}
}  // namespace

statsbackup::BookRestoreResult statsbackup::restoreBookBackup(const std::string& cachePath, const std::string& bookPath,
                                                              const std::string& filename,
                                                              const uint8_t* expectedCurrent,
                                                              size_t expectedCurrentSize, const uint8_t* expectedBackup,
                                                              size_t expectedBackupSize) {
  return replaceBookStats(cachePath, bookPath, filename, ReplacementSource::Backup, expectedCurrent,
                          expectedCurrentSize, expectedBackup, expectedBackupSize);
}

statsbackup::BookRestoreResult statsbackup::resetBookStatistics(const std::string& cachePath,
                                                                const std::string& bookPath,
                                                                const uint8_t* expectedCurrent,
                                                                size_t expectedCurrentSize) {
  return replaceBookStats(cachePath, bookPath, {}, ReplacementSource::Empty, expectedCurrent, expectedCurrentSize,
                          nullptr, 0);
}
