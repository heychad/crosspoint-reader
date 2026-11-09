#include "BookCompletion.h"

#include <FsHelpers.h>
#include <HalStorage.h>

#include <functional>

#include "ReadingStatsStore.h"

std::optional<bool> readingstats::bookCompletion(const std::string& bookPath) {
  const char* prefix = nullptr;
  if (FsHelpers::hasReflowableBookExtension(bookPath))
    prefix = "epub_";
  else if (FsHelpers::hasXtcExtension(bookPath))
    prefix = "xtc_";
  if (!prefix || !Storage.exists(bookPath.c_str())) return std::nullopt;
  const std::string cache = std::string("/.crosspoint/") + prefix + std::to_string(std::hash<std::string>{}(bookPath));
  bool tracking = true;
  const auto stats = loadBook(cache, bookPath, tracking);
  return stats.writable ? std::optional<bool>(stats.isCompleted) : std::nullopt;
}
