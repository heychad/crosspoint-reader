#include "ReadingStatsStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <ZipFile.h>

#include <array>
#include <cstring>

#include "BookTracking.h"

namespace {
HalStorage::PathStatus statsPresence(const std::string& path) {
  static constexpr std::array names = {"/stats_v5.bin", "/stats_v5.bin.tmp", "/stats_v5.bin.bak", "/stats_v4.bin",
                                       "/stats.bin"};
  const auto directory = Storage.lookupPath(path.c_str());
  if (directory != HalStorage::PathStatus::Present) return directory;
  bool present = false;
  for (const auto* name : names) {
    const auto status = Storage.lookupPath((path + name).c_str());
    if (status == HalStorage::PathStatus::Failed) return status;
    present |= status == HalStorage::PathStatus::Present;
  }
  return present ? HalStorage::PathStatus::Present : HalStorage::PathStatus::Missing;
}

bool preserveDisabledMarker(const std::string& destination) {
  const std::string marker = destination + "/reading_stats_off";
  const auto status = Storage.lookupPath(marker.c_str());
  if (status == HalStorage::PathStatus::Failed) return false;
  if (status == HalStorage::PathStatus::Present) {
    HalFile file = Storage.open(marker.c_str());
    if (!file) return false;
    const bool regular = !file.isDirectory();
    return file.close() && regular;
  }
  const auto directory = Storage.lookupPath(destination.c_str());
  if (directory == HalStorage::PathStatus::Failed ||
      (directory == HalStorage::PathStatus::Missing && !Storage.mkdir(destination.c_str())))
    return false;
  HalFile file = Storage.open(marker.c_str(), O_WRONLY | O_CREAT | O_EXCL);
  if (!file) return false;
  const uint8_t value = 1;
  const bool written = file.write(value) == 1;
  file.flush();
  const bool closed = file.close();
  if (!written || !closed) return false;
  HalFile check;
  if (!Storage.openFileForRead("STATS", marker, check)) return false;
  uint8_t actual = 0;
  const bool matches = check.fileSize() == 1 && check.read(&actual, 1) == 1 && actual == value;
  return check.close() && matches;
}

}  // namespace

std::string readingstats::bookDirectory(const std::string& cachePath) {
  const size_t slash = cachePath.find_last_of('/');
  return "/.crosspoint/reading_stats/" + cachePath.substr(slash == std::string::npos ? 0 : slash + 1);
}

BookReadingStats readingstats::loadBook(const std::string& cachePath, const std::string& bookPath, bool& enabled,
                                        bool* found, BookReadingStats::LoadMode mode, bool* trackingKnown) {
  if (found) *found = false;
  const std::string directory = bookDirectory(cachePath);
  std::string legacy;
  if (cachePath.find("/epub_") != std::string::npos) {
    legacy = "/.crosspoint/epub_" + std::to_string(ZipFile::fnvHash64(bookPath.data(), bookPath.size()));
  }
  const auto trackingState = booktracking::state(cachePath, bookPath);
  enabled = trackingState == booktracking::State::Enabled;
  if (trackingKnown) *trackingKnown = trackingState != booktracking::State::Unknown;
  // A move requires a known marker state before any repair or migration writes.
  if (trackingKnown && !*trackingKnown) return {};
  // CrossInk's EPUB cache key is FNV-1a; CrossPoint keeps its existing std::hash identity.
  const std::array<std::string, 4> directories{directory, cachePath, legacy.empty() ? "" : bookDirectory(legacy),
                                               legacy};
  for (const auto& path : directories) {
    if (path.empty()) continue;
    const auto status = statsPresence(path);
    if (status == HalStorage::PathStatus::Missing) continue;
    if (found) *found = true;
    if (status == HalStorage::PathStatus::Present) return BookReadingStats::load(path, mode);
    BookReadingStats unavailable;
    unavailable.writable = false;
    return unavailable;
  }
  return {};
}

bool readingstats::saveBook(const std::string& directory, const BookReadingStats& stats) {
  if (!stats.writable) return false;
  const auto status = Storage.lookupPath(directory.c_str());
  if (status == HalStorage::PathStatus::Failed ||
      (status == HalStorage::PathStatus::Missing && !Storage.mkdir(directory.c_str())))
    return false;
  return stats.save(directory);
}

bool readingstats::preserveCache(const std::string& cachePath) {
  const std::string destination = bookDirectory(cachePath);
  const auto marker = Storage.lookupPath((cachePath + "/reading_stats_off").c_str());
  if (marker == HalStorage::PathStatus::Failed) return false;
  const auto cacheStats = statsPresence(cachePath);
  if (cacheStats == HalStorage::PathStatus::Failed) return false;
  if (cacheStats == HalStorage::PathStatus::Present) {
    const auto stats = BookReadingStats::load(cachePath);
    if (!stats.writable) return false;
    const auto destinationStats = statsPresence(destination);
    if (destinationStats == HalStorage::PathStatus::Failed) return false;
    if (destinationStats == HalStorage::PathStatus::Missing) {
      if (!saveBook(destination, stats)) return false;
    } else if (!BookReadingStats::load(destination).writable) {
      return false;
    }
  }
  if (marker == HalStorage::PathStatus::Present && !preserveDisabledMarker(destination)) return false;
  return true;
}

readingstats::BookMove::BookMove(const std::string& oldCache, const std::string& newCache,
                                 const std::string& oldBookPath)
    : oldCache(oldCache),
      oldBookPath(oldBookPath),
      source(bookDirectory(oldCache)),
      destination(bookDirectory(newCache)) {}

bool readingstats::BookMove::prepare() {
  bool enabled = true;
  bool found = false;
  bool trackingKnown = false;
  const auto imported =
      loadBook(oldCache, oldBookPath, enabled, &found, BookReadingStats::LoadMode::Repair, &trackingKnown);
  if (!trackingKnown) return false;
  const auto sourceStats = statsPresence(source);
  if (sourceStats == HalStorage::PathStatus::Failed) return false;
  if (found && (!imported.writable || (sourceStats == HalStorage::PathStatus::Missing && !saveBook(source, imported))))
    return false;
  if (!enabled && !preserveDisabledMarker(source)) return false;
  if (!preserveCache(oldCache)) return false;
  if (Storage.lookupPath(destination.c_str()) != HalStorage::PathStatus::Missing) return false;
  const auto sourceDirectory = Storage.lookupPath(source.c_str());
  if (sourceDirectory == HalStorage::PathStatus::Failed) return false;
  if (sourceDirectory == HalStorage::PathStatus::Missing) return true;
  const auto updatedSourceStats = statsPresence(source);
  if (updatedSourceStats == HalStorage::PathStatus::Failed) return false;
  if (updatedSourceStats == HalStorage::PathStatus::Present) {
    // The original cache retains a compatible recovery copy if rollback later fails.
    const auto stats = BookReadingStats::load(source);
    if (!stats.save(oldCache)) return false;
  }
  moved = Storage.rename(source.c_str(), destination.c_str());
  return moved;
}

readingstats::BookMove::~BookMove() {
  if (moved && !Storage.rename(destination.c_str(), source.c_str())) {
    LOG_ERR("STATS", "Could not roll back stats move; original cache retains the recovery copy");
  }
}

bool readingstats::clearCache(const std::string& cachePath) {
  if (!Storage.exists(cachePath.c_str())) return true;
  if (!preserveCache(cachePath)) {
    LOG_ERR("STATS", "Keeping original statistics during cache invalidation");
  }
  auto directory = Storage.open(cachePath.c_str());
  if (!directory || !directory.isDirectory()) return false;
  char name[128];
  bool ok = true;
  for (auto entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    const size_t length = entry.getName(name, sizeof(name));
    const bool isDirectory = entry.isDirectory();
    entry.close();
    if (length == 0 || length >= sizeof(name) - 1) {
      ok = false;
      continue;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
    // Keep all versions and transaction artifacts, including opaque future data.
    if (strncmp(name, "stats_v", 7) == 0 || strncmp(name, "stats.bin", 9) == 0 ||
        strncmp(name, "reading_stats_off", 17) == 0 || strncmp(name, "reader_prefs.json", 17) == 0 ||
        strncmp(name, "reader_settings.bin", 19) == 0)
      continue;
    const std::string path = cachePath + "/" + name;
    const bool removed = isDirectory ? Storage.removeDir(path.c_str()) : Storage.remove(path.c_str());
    ok = removed && ok;
  }
  directory.close();
  return ok;
}
