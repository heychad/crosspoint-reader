#pragma once
#include <string>
#include <vector>
struct FakeRecent {
  std::string path;
};
struct FakeRecents {
  std::vector<FakeRecent> books;
  const auto& getBooks() const { return books; }
};
inline FakeRecents fakeRecents;
#define RECENT_BOOKS fakeRecents
