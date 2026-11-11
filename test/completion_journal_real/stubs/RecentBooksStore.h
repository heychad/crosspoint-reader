#pragma once
#include <string>
#include <vector>
struct TestRecentBook {
  std::string path;
};
struct TestRecentBooks {
  std::vector<TestRecentBook> books;
  bool writable = true;
  const std::vector<TestRecentBook>& getBooks() const { return books; }
  bool removeByPathPersisted(const std::string& from) {
    if (!writable) return false;
    for (auto it = books.begin(); it != books.end(); ++it)
      if (it->path == from) {
        books.erase(it);
        break;
      }
    return true;
  }
  bool updatePathPersisted(const std::string& from, const std::string& to, const std::string&, const std::string&) {
    if (!writable) return false;
    for (auto& book : books)
      if (book.path == from) book.path = to;
    return true;
  }
};
inline TestRecentBooks testRecentBooks;
#define RECENT_BOOKS testRecentBooks
