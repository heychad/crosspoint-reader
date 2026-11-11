#pragma once

#include <string_view>

// FAT and exFAT compare these ASCII folder names without case.
inline bool isInReadFolder(std::string_view path) {
  return path.size() > 6 && path[0] == '/' && (path[1] == 'r' || path[1] == 'R') &&
         (path[2] == 'e' || path[2] == 'E') && (path[3] == 'a' || path[3] == 'A') &&
         (path[4] == 'd' || path[4] == 'D') && path[5] == '/';
}
