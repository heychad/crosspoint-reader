#pragma once
#include <common/SysCall.h>

#include <cstdint>

// Require non-overlapping contiguous allocation ranges, not merely different
// first clusters: a damaged FAT chain can join a second file beyond its EOF.
// Callers must establish paths, hold the storage lock, and check close errors.
// Fragmented or unreadable chains fail closed; this does not validate the
// entire filesystem or aliases to files other than the two being compared.
template <typename File>
bool smallFilesHaveDifferentFirstClusters(File& left, File& right) {
  if (!left.isFile() || !right.isFile() || left.fileSize() == 0 || right.fileSize() == 0 || left.fileSize() > 512 ||
      right.fileSize() > 512 || left.dataLength() > 512 || right.dataLength() > 512)
    return false;
  const auto first = left.firstCluster();
  const auto second = right.firstCluster();
  if (first == 0 || second == 0 || first == second) return false;
  Sector_t leftFirst = 0, leftLast = 0, rightFirst = 0, rightLast = 0;
  if (!left.contiguousRange(&leftFirst, &leftLast) || !right.contiguousRange(&rightFirst, &rightLast) ||
      leftFirst > leftLast || rightFirst > rightLast)
    return false;
  return leftLast < rightFirst || rightLast < leftFirst;
}
