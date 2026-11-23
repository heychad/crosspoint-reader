#include "BookmarkUtil.h"

#include <CheckedStoragePath.h>

#include <algorithm>
#include <string>

std::string BookmarkUtil::getBookmarksDir() { return "/.crosspoint/bookmarks/"; }

std::string BookmarkUtil::getBookmarkPath(const std::string& bookPath) {
  // remove leading slash and replace internal slashes to create a flat filename
  std::string bookName = std::string(bookPath).erase(0, 1);
  std::replace(bookName.begin(), bookName.end(), '/', '_');
  std::replace(bookName.begin(), bookName.end(), '\\', '_');
  const size_t lastDot = bookName.find_last_of('.');
  if (lastDot != std::string::npos) {
    bookName.erase(lastDot);
  }
  bookName += ".json";
  return getBookmarksDir() + bookName;
}

std::string BookmarkUtil::nativeRecoveryPath(const std::string& primary, bool backup) {
  auto path = primary + (backup ? ".bak" : ".tmp");
  if (validCheckedStorageComponent(std::string_view(path).substr(path.find_last_of('/') + 1))) return path;
  // Replacing the five-character extension keeps a maximum-length name valid.
  return primary.substr(0, primary.size() - 5) + (backup ? ".jbak" : ".jtmp");
}

std::string BookmarkUtil::sanitizeBookmarkSummary(std::string summary) {
  summary.erase(
      std::unique(summary.begin(), summary.end(), [](char a, char b) { return std::isspace(a) && std::isspace(b); }),
      summary.end());
  summary.erase(std::remove(summary.begin(), summary.end(), '\n'), summary.end());
  summary.erase(summary.begin(),
                std::find_if(summary.begin(), summary.end(), [](unsigned char ch) { return !std::isspace(ch); }));
  summary.erase(
      std::find_if(summary.rbegin(), summary.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
      summary.end());
  if (summary.size() > 72) {
    summary.resize(72);
  }
  return summary;
}
