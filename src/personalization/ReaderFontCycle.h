#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Caller holds the render lock and keeps the family registry unchanged.
// Family names must fit the persisted setting without truncation.
namespace readerfontcycle {
template <typename Families>
bool advance(uint8_t& builtin, uint8_t builtinCount, char* current, size_t capacity, const Families& families) {
  if (!builtinCount || !current || !capacity || std::memchr(current, '\0', capacity) == nullptr) return false;
  const size_t total = static_cast<size_t>(builtinCount) + families.size();
  size_t index = builtin < builtinCount ? builtin : 0;
  if (*current) {
    index = total;
    for (size_t i = 0; i < families.size(); ++i) {
      if (families[i].name == current) {
        index = static_cast<size_t>(builtinCount) + i;
        break;
      }
    }
    // Discovery may have failed; keep an unrecognized selection intact.
    if (index == total) return false;
  }
  for (size_t step = 1; step <= total; ++step) {
    const size_t next = (index + step) % total;
    if (next < builtinCount) {
      if (!*current && builtin == next) return false;
      builtin = static_cast<uint8_t>(next);
      current[0] = '\0';
      return true;
    }
    const auto& name = families[next - builtinCount].name;
    if (name.empty() || name.size() >= capacity || name.find('\0') != name.npos) continue;
    if (name == current) continue;
    std::memcpy(current, name.c_str(), name.size() + 1);
    return true;
  }
  return false;
}
}  // namespace readerfontcycle
