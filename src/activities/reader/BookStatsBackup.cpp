#include "BookStatsBackup.h"

#include <Arduino.h>
#include <HalStorage.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include "BookReadingStats.h"
#include "ReadingStatsStore.h"

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
}  // namespace

bool statsbackup::createBookBackup(const std::string& cachePath, const BookReadingStats& stats) {
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
  return readMatches(target, bytes);
}
