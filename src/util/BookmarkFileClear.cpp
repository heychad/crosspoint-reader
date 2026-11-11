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
    for (const auto* suffix : {".tmp", ".bak", ""}) {
      const auto target = path + suffix;
      const auto status = Storage.lookupPath(target.c_str());
      if (status == HalStorage::PathStatus::Failed ||
          (status == HalStorage::PathStatus::Present && !Storage.remove(target.c_str())))
        return false;
    }
    return true;
  };
  return Storage.withStorageLock(clear, &request);
}
