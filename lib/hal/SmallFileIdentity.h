#pragma once
#include <cstdint>

// Check only the data each small file can address while rewritten in place.
// Callers must establish paths, hold the storage lock, and check close errors.
// This does not validate filesystem integrity or aliases to other files.
template <typename File>
bool smallFilesHaveDifferentFirstClusters(const File& left, const File& right) {
  if (!left.isFile() || !right.isFile() || left.fileSize() == 0 || right.fileSize() == 0 || left.fileSize() > 512 ||
      right.fileSize() > 512 || left.dataLength() > 512 || right.dataLength() > 512)
    return false;
  const auto first = left.firstCluster();
  const auto second = right.firstCluster();
  return first != 0 && second != 0 && first != second;
}
