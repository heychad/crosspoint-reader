#pragma once
#include <string>
struct TestRecentBooksStore {
  bool success = true;
  int calls = 0;
  std::string path;
  bool removeByPathPersisted(const std::string& value) {
    ++calls;
    path = value;
    return success;
  }
};
inline TestRecentBooksStore RECENT_BOOKS;
