#include "BookTracking.h"

#include <HalStorage.h>
#include <ZipFile.h>

#include <array>

#include "ReadingStatsStore.h"

namespace {
std::array<std::string, 4> markers(const std::string& cache, const std::string& book) {
  std::array<std::string, 4> paths{readingstats::bookDirectory(cache) + "/reading_stats_off",
                                   cache + "/reading_stats_off", "", ""};
  if (cache.find("/epub_") != std::string::npos) {
    const std::string legacy = "/.crosspoint/epub_" + std::to_string(ZipFile::fnvHash64(book.data(), book.size()));
    paths[2] = legacy + "/reading_stats_off";
    paths[3] = readingstats::bookDirectory(legacy) + "/reading_stats_off";
  }
  return paths;
}

bool inspect(const std::array<std::string, 4>& paths, std::array<bool, 4>& found) {
  for (size_t i = 0; i < paths.size(); ++i) {
    const auto& path = paths[i];
    if (path.empty()) continue;
    bool duplicate = false;
    for (size_t j = 0; j < i; ++j) duplicate |= path == paths[j];
    if (duplicate) continue;
    const auto status = Storage.lookupPath(path.c_str());
    if (status == HalStorage::PathStatus::Failed) return false;
    if (status == HalStorage::PathStatus::Missing) continue;
    HalFile file = Storage.open(path.c_str());
    if (!file) return false;
    const bool regular = !file.isDirectory();
    if (!file.close() || !regular) return false;
    found[i] = true;
  }
  return true;
}
}  // namespace

booktracking::State booktracking::state(const std::string& cachePath, const std::string& bookPath) {
  if (cachePath.empty() || bookPath.empty()) return State::Unknown;
  std::array<bool, 4> found{};
  if (!inspect(markers(cachePath, bookPath), found)) return State::Unknown;
  for (const bool present : found)
    if (present) return State::Disabled;
  return State::Enabled;
}

bool booktracking::setEnabled(const std::string& cachePath, const std::string& bookPath, bool enabled) {
  if (cachePath.empty() || bookPath.empty()) return false;
  const auto paths = markers(cachePath, bookPath);
  std::array<bool, 4> found{};
  if (!inspect(paths, found)) return false;
  if (enabled) {
    for (size_t i = 0; i < paths.size(); ++i)
      if (found[i] && !Storage.remove(paths[i].c_str())) return false;
    return state(cachePath, bookPath) == State::Enabled;
  }
  for (const bool present : found)
    if (present) return true;
  const auto dir = readingstats::bookDirectory(cachePath);
  const auto directoryStatus = Storage.lookupPath(dir.c_str());
  if (directoryStatus == HalStorage::PathStatus::Failed ||
      (directoryStatus == HalStorage::PathStatus::Missing && !Storage.mkdir(dir.c_str())))
    return false;
  HalFile file = Storage.open(paths.front().c_str(), O_WRONLY | O_CREAT | O_EXCL);
  if (!file) return false;
  const uint8_t value = 1;
  const bool written = file.write(value) == 1;
  file.flush();
  const bool closed = file.close();
  if (!written || !closed) return false;
  HalFile check;
  if (!Storage.openFileForRead("STATS", paths.front(), check)) return false;
  uint8_t actual = 0;
  const bool matches = check.fileSize() == 1 && check.read(&actual, 1) == 1 && actual == value;
  return check.close() && matches && state(cachePath, bookPath) == State::Disabled;
}
