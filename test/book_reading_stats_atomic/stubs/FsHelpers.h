#pragma once
#include <string_view>
namespace FsHelpers {
inline bool hasReflowableBookExtension(std::string_view path) {
  return path.ends_with(".epub") || path.ends_with(".txt") || path.ends_with(".md");
}
inline bool hasXtcExtension(std::string_view path) { return path.ends_with(".xtc") || path.ends_with(".xtch"); }
}  // namespace FsHelpers
