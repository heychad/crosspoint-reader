#pragma once

#include <HalStorage.h>

#include <utility>

#include "BookmarkUtil.h"

// Caller holds the storage lock. A confirmed missing primary can use a backup
// or a first-save staging file. An uncertain lookup never selects older data.
inline HalStorage::PathStatus selectNativeBookmarkFile(std::string& path) {
  auto status = Storage.lookupPath(path.c_str());
  if (status != HalStorage::PathStatus::Missing) return status;
  for (const bool backup : {true, false}) {
    auto candidate = BookmarkUtil::nativeRecoveryPath(path, backup);
    status = Storage.lookupPath(candidate.c_str());
    if (status == HalStorage::PathStatus::Failed) return status;
    if (status == HalStorage::PathStatus::Present) {
      path = std::move(candidate);
      return status;
    }
  }
  return HalStorage::PathStatus::Missing;
}
