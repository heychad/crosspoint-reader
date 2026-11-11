#pragma once

#include <cstdint>
#include <string_view>

#include "StageName.h"

namespace syncstage {
inline constexpr char ID_SCOPE[] = "xteink-book-sync";
inline constexpr uint64_t MAX_STAGE_BYTES = 0x7fffffffULL - 1024;
inline bool parseSize(std::string_view text, uint64_t& output) {
  if (text.empty() || (text.size() > 1 && text.front() == '0')) return false;
  uint64_t value = 0;
  for (const char ch : text) {
    if (ch < '0' || ch > '9') return false;
    const unsigned digit = ch - '0';
    if (value > (MAX_STAGE_BYTES - digit) / 10) return false;
    value = value * 10 + digit;
  }
  output = value;
  return true;
}
inline bool parseDigest(std::string_view text, uint8_t (&output)[32]) {
  if (text.size() != 64) return false;
  uint8_t parsed[32];
  const auto hex = [](char ch) -> int {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < 32; ++i) {
    const int high = hex(text[i * 2]);
    const int low = hex(text[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    parsed[i] = static_cast<uint8_t>((high << 4) | low);
  }
  for (size_t i = 0; i < 32; ++i) output[i] = parsed[i];
  return true;
}
}  // namespace syncstage
