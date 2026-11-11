#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "CompletionJournal.h"
#include "CompletionMoveFile.h"

// Immutable intent for a future recoverable finished-book move. A plan must
// be durably published before any rename and bound to its completion record.
// This codec performs no storage operations and does not authorize a move.
namespace readingstats::completionmove {
constexpr size_t MAX_PATH_BYTES = completionjournal::MAX_SOURCE_PATH;
constexpr size_t HEADER_BYTES = 18;
constexpr size_t MOVED_FILE_COUNT = 11;
constexpr size_t STATE_FILE_END = 30;
constexpr size_t LEGACY_BOOKMARK_END = 32;
constexpr size_t FILE_COUNT = 51;
constexpr size_t IDENTITY_BYTES = 17;
constexpr size_t IDENTITIES_BYTES = FILE_COUNT * IDENTITY_BYTES;
constexpr size_t CHECKSUM_BYTES = 8;
constexpr size_t MAX_PLAN_BYTES = HEADER_BYTES + IDENTITIES_BYTES + 2 * MAX_PATH_BYTES + CHECKSUM_BYTES;
using Plan = std::array<uint8_t, MAX_PLAN_BYTES>;

inline bool validPath(std::string_view path) { return completionjournal::validSourcePath(path); }

inline bool encode(std::string_view from, std::string_view to, uint64_t completionHash, Plan& output, size_t& size,
                   const FileIdentity* identities = nullptr) {
  size = 0;
  if (!validPath(from) || !validPath(to) || from == to || !to.starts_with("/read/") ||
      to.find('/', 6) != std::string_view::npos)
    return false;
  output.fill(0);
  constexpr uint8_t magic[] = {'C', 'P', 'M', 'V', 2, 0};
  std::memcpy(output.data(), magic, sizeof(magic));
  output[6] = static_cast<uint8_t>(from.size());
  output[7] = static_cast<uint8_t>(from.size() >> 8);
  output[8] = static_cast<uint8_t>(to.size());
  output[9] = static_cast<uint8_t>(to.size() >> 8);
  for (size_t i = 0; i < 8; ++i) output[10 + i] = static_cast<uint8_t>(completionHash >> (8 * i));
  size_t offset = HEADER_BYTES;
  for (size_t i = 0; i < FILE_COUNT; ++i) {
    const FileIdentity identity = identities ? identities[i] : FileIdentity{};
    if (!identity.present && (identity.size || identity.checksum)) return false;
    output[offset++] = identity.present ? 1 : 0;
    for (size_t j = 0; j < 8; ++j) output[offset++] = static_cast<uint8_t>(identity.size >> (8 * j));
    for (size_t j = 0; j < 8; ++j) output[offset++] = static_cast<uint8_t>(identity.checksum >> (8 * j));
  }
  std::memcpy(output.data() + offset, from.data(), from.size());
  offset += from.size();
  std::memcpy(output.data() + offset, to.data(), to.size());
  offset += to.size();
  const auto checksum = completionjournal::checksum(output.data(), offset);
  for (size_t i = 0; i < CHECKSUM_BYTES; ++i) output[offset + i] = static_cast<uint8_t>(checksum >> (8 * i));
  size = offset + CHECKSUM_BYTES;
  return true;
}

// Outputs borrow bytes and change only after all structural checks pass.
inline bool decode(const uint8_t* bytes, size_t size, uint64_t completionHash, std::string_view& from,
                   std::string_view& to, FileIdentity* identities = nullptr) {
  constexpr uint8_t magic[] = {'C', 'P', 'M', 'V', 2, 0};
  if (!bytes || size < HEADER_BYTES + IDENTITIES_BYTES + CHECKSUM_BYTES || size > MAX_PLAN_BYTES ||
      std::memcmp(bytes, magic, sizeof(magic)) != 0)
    return false;
  for (size_t i = 0; i < 8; ++i)
    if (bytes[10 + i] != static_cast<uint8_t>(completionHash >> (8 * i))) return false;
  const size_t fromSize = bytes[6] | (static_cast<size_t>(bytes[7]) << 8);
  const size_t toSize = bytes[8] | (static_cast<size_t>(bytes[9]) << 8);
  if (fromSize > MAX_PATH_BYTES || toSize > MAX_PATH_BYTES ||
      size != HEADER_BYTES + IDENTITIES_BYTES + fromSize + toSize + CHECKSUM_BYTES)
    return false;
  uint64_t stored = 0;
  for (size_t i = 0; i < CHECKSUM_BYTES; ++i) stored |= uint64_t{bytes[size - CHECKSUM_BYTES + i]} << (8 * i);
  if (stored != completionjournal::checksum(bytes, size - CHECKSUM_BYTES)) return false;
  size_t offset = HEADER_BYTES;
  for (size_t i = 0; i < FILE_COUNT; ++i) {
    if (bytes[offset] > 1) return false;
    FileIdentity identity;
    identity.present = bytes[offset++] == 1;
    for (size_t j = 0; j < 8; ++j) identity.size |= uint64_t{bytes[offset++]} << (8 * j);
    for (size_t j = 0; j < 8; ++j) identity.checksum |= uint64_t{bytes[offset++]} << (8 * j);
    if (!identity.present && (identity.size || identity.checksum)) return false;
  }
  const std::string_view source(reinterpret_cast<const char*>(bytes + offset), fromSize);
  const std::string_view destination(reinterpret_cast<const char*>(bytes + offset + fromSize), toSize);
  if (!validPath(source) || !validPath(destination) || source == destination || !destination.starts_with("/read/") ||
      destination.find('/', 6) != std::string_view::npos)
    return false;
  from = source;
  to = destination;
  if (identities) {
    offset = HEADER_BYTES;
    for (size_t i = 0; i < FILE_COUNT; ++i) {
      FileIdentity identity;
      identity.present = bytes[offset++] == 1;
      for (size_t j = 0; j < 8; ++j) identity.size |= uint64_t{bytes[offset++]} << (8 * j);
      for (size_t j = 0; j < 8; ++j) identity.checksum |= uint64_t{bytes[offset++]} << (8 * j);
      identities[i] = identity;
    }
  }
  return true;
}
}  // namespace readingstats::completionmove
