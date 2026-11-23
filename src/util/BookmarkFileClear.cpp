#include <HalStorage.h>

#include "BookmarkFile.h"
#include "BookmarkUtil.h"
#include "CrossInkBookmarks.h"

bool BookmarkFile::clearForBook(const std::string& bookPath) {
  if (bookPath.empty()) return false;
  struct Request {
    const std::string& path;
  } request{bookPath};
  const auto clear = [](void* raw) {
    const auto& book = static_cast<Request*>(raw)->path;
    if (!CrossInkBookmarks::clearForBook(book)) return false;
    const auto path = BookmarkUtil::getBookmarkPath(book);
    const std::string targets[] = {BookmarkUtil::nativeRecoveryPath(path, false),
                                   BookmarkUtil::nativeRecoveryPath(path, true), path};
    bool present[3]{};
    for (unsigned i = 0; i < 3; ++i) {
      const auto status = Storage.lookupPath(targets[i].c_str());
      if (status == HalStorage::PathStatus::Failed) return false;
      present[i] = status == HalStorage::PathStatus::Present;
      for (unsigned j = 0; j < i; ++j) {
        if (present[i] && present[j] && !Storage.distinctMoveChains(targets[i].c_str(), targets[j].c_str()))
          return false;
      }
    }
    for (unsigned i = 0; i < 3; ++i) {
      if (present[i] && !Storage.remove(targets[i].c_str())) return false;
    }
    return true;
  };
  return Storage.withStorageLock(clear, &request);
}
