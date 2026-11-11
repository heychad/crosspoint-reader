#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

// Reject invalid or ambiguous FAT components before a lookup can report Missing.
// FAT long names are limited to 255 UTF-16 code units.
inline bool validCheckedStorageComponent(std::string_view name) {
  if (name.empty() || name.front() == ' ') return false;
  const size_t length = name.size();
  if (name[length - 1] == ' ' || name[length - 1] == '.') return false;
  if (name == "." || name == "..") return false;
  const auto* bytes = reinterpret_cast<const unsigned char*>(name.data());
  size_t offset = 0;
  size_t units = 0;
  while (offset < length) {
    const unsigned char first = bytes[offset];
    uint32_t codepoint = 0;
    size_t count = 0;
    if (first < 0x80) {
      codepoint = first;
      count = 1;
    } else if (first >= 0xc2 && first <= 0xdf) {
      codepoint = first & 0x1f;
      count = 2;
    } else if (first >= 0xe0 && first <= 0xef) {
      codepoint = first & 0x0f;
      count = 3;
    } else if (first >= 0xf0 && first <= 0xf4) {
      codepoint = first & 0x07;
      count = 4;
    } else {
      return false;
    }
    if (count > length - offset) return false;
    for (size_t i = 1; i < count; ++i) {
      if ((bytes[offset + i] & 0xc0) != 0x80) return false;
      codepoint = (codepoint << 6) | (bytes[offset + i] & 0x3f);
    }
    if ((count == 3 && codepoint < 0x800) || (count == 4 && codepoint < 0x10000) ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff)
      return false;
    if (codepoint < 0x20 || codepoint == '"' || codepoint == '*' || codepoint == '/' || codepoint == ':' ||
        codepoint == '<' || codepoint == '>' || codepoint == '?' || codepoint == '\\' || codepoint == '|')
      return false;
    units += codepoint > 0xffff ? 2 : 1;
    if (units > 255) return false;
    offset += count;
  }
  return true;
}
inline bool validCheckedStorageComponent(const char* name) {
  return name && validCheckedStorageComponent(std::string_view(name));
}
