#pragma once

#include <cstddef>

namespace settingsrecovery {
// Checks the object boundary only; a JSON parser must also validate syntax.
inline bool completeObjectEnvelope(const char* bytes, size_t size) {
  if (!bytes) return false;
  const auto whitespace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
  size_t pos = 0;
  while (pos < size && whitespace(bytes[pos])) ++pos;
  if (pos == size || bytes[pos] != '{') return false;
  size_t depth = 0;
  bool string = false, escaped = false;
  for (; pos < size; ++pos) {
    const char c = bytes[pos];
    if (string) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        string = false;
    } else if (c == '"') {
      string = true;
    } else if (c == '{') {
      ++depth;
    } else if (c == '}') {
      if (--depth == 0) {
        ++pos;
        while (pos < size && whitespace(bytes[pos])) ++pos;
        return pos == size;
      }
    }
  }
  return false;
}
}  // namespace settingsrecovery
