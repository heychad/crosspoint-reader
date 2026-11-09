#pragma once

#include <map>
#include <optional>
#include <string>

#include "HalStorage.h"

struct FakeMetadata {
  std::string title = "Title";
  std::string author = "Author";
  bool success = true;
  std::string series;
  std::string genre;
  std::optional<float> seriesIndex;
};

inline std::map<std::string, FakeMetadata> bookMetadata;

class Epub {
  std::string path;

 public:
  Epub(const std::string& path, const char*) : path(path) {}

  bool loadMetadata(std::string& title, std::string& author, std::string* series = nullptr,
                    std::string* genre = nullptr, std::optional<float>* position = nullptr) {
    ++fake::parses;
    const auto& metadata = bookMetadata[path];
    if (!metadata.success) return false;
    title = metadata.title;
    author = metadata.author;
    if (series) *series = metadata.series;
    if (genre) *genre = metadata.genre;
    if (position) *position = metadata.seriesIndex;
    return true;
  }
};
