#pragma once
#include <Logging.h>
#include <Memory.h>

#include <cstddef>
#include <cstdint>

// Require a checked single-cluster chain, not merely different first clusters:
// a damaged FAT chain can join another file beyond the declared end of data.
// Callers must establish paths, hold the storage lock, and check close errors.
// Longer or unreadable chains fail closed; this does not validate the
// entire filesystem or aliases to files other than the two being compared.
template <typename File>
bool smallFilesHaveDifferentFirstClusters(File& left, File& right) {
  if (!left.isFile() || !right.isFile() || left.fileSize() == 0 || right.fileSize() == 0 || left.fileSize() > 512 ||
      right.fileSize() > 512 || left.dataLength() > 512 || right.dataLength() > 512)
    return false;
  const auto first = left.firstCluster();
  const auto second = right.firstCluster();
  if (first == 0 || second == 0 || first == second) return false;
  return left.smallSingleClusterChain() && right.smallSingleClusterChain();
}

// Settings snapshots may exceed 512 bytes. Both files must still fit within
// one actual allocation cluster, bounded to 32 KiB and verified separately.
template <typename File>
bool settingsFilesHaveDifferentFirstClusters(File& left, File& right) {
  if (!left.isFile() || !right.isFile() || left.fileSize() == 0 || right.fileSize() == 0 || left.fileSize() > 32768 ||
      right.fileSize() > 32768 || left.dataLength() > 32768 || right.dataLength() > 32768)
    return false;
  const auto first = left.firstCluster();
  const auto second = right.firstCluster();
  if (first == 0 || second == 0 || first == second) return false;
  return left.settingsSingleClusterChain() && right.settingsSingleClusterChain();
}

template <typename File>
bool settingsFileHasCheckedChain(File& file) {
  auto clusters = makeUniqueNoThrow<uint32_t[]>(64);
  if (!clusters) {
    LOG_ERR("STORAGE", "OOM allocating %u bytes for settings chain", static_cast<unsigned>(64 * sizeof(uint32_t)));
    return false;
  }
  size_t count = 0;
  return file.settingsClusterList(clusters.get(), 64, &count) && count != 0;
}

template <typename File>
bool settingsFilesHaveDisjointChains(File& left, File& right) {
  auto a = makeUniqueNoThrow<uint32_t[]>(64);
  if (!a) {
    LOG_ERR("STORAGE", "OOM allocating %u bytes for settings chain", static_cast<unsigned>(64 * sizeof(uint32_t)));
    return false;
  }
  auto b = makeUniqueNoThrow<uint32_t[]>(64);
  if (!b) {
    LOG_ERR("STORAGE", "OOM allocating %u bytes for settings chain", static_cast<unsigned>(64 * sizeof(uint32_t)));
    return false;
  }
  size_t aCount = 0, bCount = 0;
  if (!left.settingsClusterList(a.get(), 64, &aCount) || !right.settingsClusterList(b.get(), 64, &bCount) ||
      aCount == 0 || bCount == 0)
    return false;
  for (size_t i = 0; i < aCount; ++i)
    for (size_t j = 0; j < bCount; ++j)
      if (a[i] == b[j]) return false;
  return true;
}
