#pragma once

#include <HalStorage.h>

#include <utility>

#include "BookmarkUtil.h"

enum class NativeBookmarkSource { Primary, Backup, Staged, Missing, Unavailable };

// Caller holds the storage lock. An uncertain lookup never selects older data.
inline NativeBookmarkSource selectNativeBookmarkFile(std::string& path) {
  auto status = Storage.lookupPath(path.c_str());
  if (status == HalStorage::PathStatus::Present) return NativeBookmarkSource::Primary;
  if (status == HalStorage::PathStatus::Failed) return NativeBookmarkSource::Unavailable;
  for (const bool backup : {true, false}) {
    auto candidate = BookmarkUtil::nativeRecoveryPath(path, backup);
    status = Storage.lookupPath(candidate.c_str());
    if (status == HalStorage::PathStatus::Failed) return NativeBookmarkSource::Unavailable;
    if (status == HalStorage::PathStatus::Present) {
      path = std::move(candidate);
      return backup ? NativeBookmarkSource::Backup : NativeBookmarkSource::Staged;
    }
  }
  return NativeBookmarkSource::Missing;
}
