#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "SmallFileIdentity.h"

// Used only for leftover clipping files. A maximal v4 store is 4,366,351
// bytes. The 5 MiB ceiling bounds both the walk and its temporary workspace.
template <typename File, typename Allocate>
bool clippingFilesHaveIndependentStorage(File& left, File& right, Allocate allocate) {
  constexpr uint64_t smallLimit = 256U * 1024U;
  constexpr uint64_t largeLimit = 5U * 1024U * 1024U;
  const auto leftSize = left.fileSize(), rightSize = right.fileSize();
  if (leftSize <= smallLimit && rightSize <= smallLimit) return moveFilesHaveIndependentStorage(left, right);
  if (!left.isFile() || !right.isFile() || leftSize > largeLimit || rightSize > largeLimit ||
      !left.moveDifferentEntry(right))
    return false;

  // A cluster holds at least 512 bytes. Only large-file cleanup requests
  // more than 512 cluster IDs from SdFat. Both arrays together use <=80 KiB.
  const auto capacity = [](uint64_t bytes) -> size_t { return static_cast<size_t>((bytes + 511) / 512); };
  const size_t leftCapacity = capacity(leftSize), rightCapacity = capacity(rightSize);
  auto storage = allocate((leftCapacity + rightCapacity) * sizeof(uint32_t));
  if (!storage) return false;
  auto* leftIds = reinterpret_cast<uint32_t*>(storage.get());
  auto* rightIds = leftIds + leftCapacity;
  const auto collect = [](File& file, uint32_t* ids, size_t cap, size_t& count) {
    if (cap == 0) return isUnallocatedEmptyFile(file);
    if (!file.settingsClusterList(ids, cap, &count) || count == 0 || count > cap) return false;
    std::sort(ids, ids + count);
    return std::adjacent_find(ids, ids + count) == ids + count;
  };
  size_t leftCount = 0, rightCount = 0;
  if (!collect(left, leftIds, leftCapacity, leftCount) || !collect(right, rightIds, rightCapacity, rightCount))
    return false;
  size_t i = 0, j = 0;
  while (i < leftCount && j < rightCount) {
    if (leftIds[i] == rightIds[j]) return false;
    if (leftIds[i] < rightIds[j]) {
      ++i;
    } else {
      ++j;
    }
  }
  return true;
}
