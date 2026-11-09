#pragma once
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
  if (!left.isFile() || !right.isFile() || left.fileSize() == 0 || right.fileSize() == 0 ||
      left.fileSize() > 32768 || right.fileSize() > 32768 || left.dataLength() > 32768 ||
      right.dataLength() > 32768)
    return false;
  const auto first = left.firstCluster();
  const auto second = right.firstCluster();
  if (first == 0 || second == 0 || first == second) return false;
  return left.settingsSingleClusterChain() && right.settingsSingleClusterChain();
}
