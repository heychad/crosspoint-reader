#pragma once

#include <string_view>

namespace syncstage {
inline bool validName(std::string_view name) {
  constexpr std::string_view prefix = "__xsync_";
  constexpr std::string_view suffix = ".part";
  if (name.size() != prefix.size() + 32 + suffix.size() || !name.starts_with(prefix) || !name.ends_with(suffix))
    return false;
  for (const char ch : name.substr(prefix.size(), 32))
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
  return true;
}
}  // namespace syncstage
