#pragma once

#include <CheckedStoragePath.h>
#include <CredentialIntegrity.h>
#include <HiddenItems.h>
#include <strings.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "StageRequest.h"

// Codec only: writing this record does not make a publication safe or durable.
namespace syncpublish {
constexpr size_t STAGE_BYTES = 45;
constexpr size_t MAX_DESTINATION = 1024;
constexpr size_t HEADER_BYTES = 4 + 8 + 2 + STAGE_BYTES + 32;
constexpr size_t MAX_RECORD = HEADER_BYTES + MAX_DESTINATION + 4;
using Record = std::array<uint8_t, MAX_RECORD>;
using Digest = std::array<uint8_t, 32>;
struct IntentView {
  std::string_view stage;
  std::string_view destination;
  uint64_t size;
  Digest digest;
};

inline bool validDestination(std::string_view path) {
  if (path.size() < 2 || path.size() > MAX_DESTINATION || path.front() != '/' || path.back() == '/') return false;
  size_t depth = 0;
  for (size_t start = 1; start < path.size();) {
    const auto slash = path.find('/', start);
    const size_t end = slash == path.npos ? path.size() : slash;
    const auto part = path.substr(start, end - start);
    if (part.empty() || part.size() > 765 || part.front() == '.' || ++depth > 16) return false;
    if (!validCheckedStorageComponent(part)) return false;
    for (const char* hidden : FsHelpers::HIDDEN_ITEMS)
      if (strlen(hidden) == part.size() && strncasecmp(part.data(), hidden, part.size()) == 0) return false;
    for (size_t i = 0; i + 1 < part.size(); ++i)
      if (part[i] == '~' && part[i + 1] >= '0' && part[i + 1] <= '9') return false;
    for (const unsigned char ch : part)
      if (ch == 0x7f) return false;
    if (slash == path.npos) break;
    start = slash + 1;
  }
  constexpr std::string_view extension = ".epub";
  if (path.size() < extension.size()) return false;
  return strncasecmp(path.data() + path.size() - extension.size(), extension.data(), extension.size()) == 0;
}

inline bool valid(const IntentView& intent) {
  return intent.stage.size() == STAGE_BYTES && syncstage::validName(intent.stage) &&
         intent.size <= syncstage::MAX_STAGE_BYTES && validDestination(intent.destination);
}
inline void putNumber(uint8_t* out, uint64_t value, size_t count) {
  for (size_t i = 0; i < count; ++i) out[i] = static_cast<uint8_t>(value >> (8 * i));
}
inline uint64_t getNumber(const uint8_t* in, size_t count) {
  uint64_t value = 0;
  for (size_t i = 0; i < count; ++i) value |= static_cast<uint64_t>(in[i]) << (8 * i);
  return value;
}
inline bool encode(const IntentView& intent, Record& output, size_t& length) {
  if (!valid(intent)) return false;
  memcpy(output.data(), "XSP1", 4);
  putNumber(output.data() + 4, intent.size, 8);
  putNumber(output.data() + 12, intent.destination.size(), 2);
  memcpy(output.data() + 14, intent.stage.data(), STAGE_BYTES);
  memcpy(output.data() + 14 + STAGE_BYTES, intent.digest.data(), intent.digest.size());
  memcpy(output.data() + HEADER_BYTES, intent.destination.data(), intent.destination.size());
  const size_t contentLength = HEADER_BYTES + intent.destination.size();
  const auto bytes = std::string_view(reinterpret_cast<const char*>(output.data()), contentLength);
  putNumber(output.data() + contentLength, credential_integrity::crc32(bytes), 4);
  length = contentLength + 4;
  return true;
}
// The returned views borrow the record. Invalid data leaves output unchanged.
inline bool decode(const uint8_t* bytes, size_t length, IntentView& output) {
  if (!bytes || length < HEADER_BYTES + 5 || length > MAX_RECORD || memcmp(bytes, "XSP1", 4) != 0) return false;
  const size_t pathLength = getNumber(bytes + 12, 2);
  if (!pathLength || pathLength > MAX_DESTINATION || length != HEADER_BYTES + pathLength + 4) return false;
  const auto contents = std::string_view(reinterpret_cast<const char*>(bytes), length - 4);
  if (credential_integrity::crc32(contents) != getNumber(bytes + length - 4, 4)) return false;
  IntentView candidate{std::string_view(reinterpret_cast<const char*>(bytes + 14), STAGE_BYTES),
                       std::string_view(reinterpret_cast<const char*>(bytes + HEADER_BYTES), pathLength),
                       getNumber(bytes + 4, 8),
                       {}};
  memcpy(candidate.digest.data(), bytes + 14 + STAGE_BYTES, candidate.digest.size());
  if (!valid(candidate)) return false;
  output = candidate;
  return true;
}
}  // namespace syncpublish
