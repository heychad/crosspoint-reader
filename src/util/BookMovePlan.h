#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "activities/reader/CompletionJournal.h"
#include "network/sync/ReferencePath.h"

namespace bookmove {
enum class EntryKind : uint8_t { Missing, File, Directory, LegacyBookmark };
struct EntryIdentity {
  EntryKind kind = EntryKind::Missing;
  uint32_t firstCluster = 0;
  uint64_t size = 0;
  uint64_t checksum = 0;
};

constexpr size_t ENTRY_COUNT = 27;
constexpr size_t FIRST_LEGACY_BOOKMARK = 11;
constexpr size_t FIRST_DIRECTORY = 21;
constexpr size_t MAX_PATH_BYTES = readingstats::completionjournal::MAX_SOURCE_PATH;
constexpr size_t HEADER_BYTES = 18;
constexpr size_t ENTRY_BYTES = 21;
constexpr size_t CHECKSUM_BYTES = 8;
constexpr size_t MAX_PLAN_BYTES = HEADER_BYTES + ENTRY_COUNT * ENTRY_BYTES + 2 * MAX_PATH_BYTES + CHECKSUM_BYTES;
using PlanBytes = std::array<uint8_t, MAX_PLAN_BYTES>;
using Identities = std::array<EntryIdentity, ENTRY_COUNT>;

inline bool validMovePath(std::string_view path) {
  if (path.size() > MAX_PATH_BYTES || !syncpublish::validReferencePath(path)) return false;
  size_t start = 1;
  while (start < path.size()) {
    if (path[start] == '.') return false;
    const auto next = path.find('/', start);
    if (next == path.npos) break;
    start = next + 1;
  }
  return true;
}

inline bool validIdentity(const EntryIdentity& identity) {
  switch (identity.kind) {
    case EntryKind::Missing:
      return identity.firstCluster == 0 && identity.size == 0 && identity.checksum == 0;
    case EntryKind::Directory:
      return identity.firstCluster != 0 && identity.size == 0 && identity.checksum == 0;
    case EntryKind::File:
    case EntryKind::LegacyBookmark:
      return identity.firstCluster == 0;
  }
  return false;
}

constexpr EntryKind expectedKind(size_t index) {
  return index < FIRST_LEGACY_BOOKMARK ? EntryKind::File
         : index < FIRST_DIRECTORY     ? EntryKind::LegacyBookmark
                                       : EntryKind::Directory;
}

inline void put64(uint8_t* out, uint64_t value) {
  for (size_t i = 0; i < 8; ++i) out[i] = static_cast<uint8_t>(value >> (8 * i));
}
inline uint64_t get64(const uint8_t* in) {
  uint64_t value = 0;
  for (size_t i = 0; i < 8; ++i) value |= uint64_t{in[i]} << (8 * i);
  return value;
}

inline bool encodePlan(std::string_view from, std::string_view to, uint64_t id, const Identities& entries,
                       PlanBytes& output, size_t& size) {
  size = 0;
  if (!validMovePath(from) || !validMovePath(to) || from == to || id == 0) return false;
  for (size_t i = 0; i < entries.size(); ++i)
    if (!validIdentity(entries[i]) || (entries[i].kind != EntryKind::Missing && entries[i].kind != expectedKind(i)))
      return false;
  if (entries[0].kind != EntryKind::File) return false;
  output.fill(0);
  constexpr uint8_t magic[] = {'C', 'P', 'B', 'M', 1, static_cast<uint8_t>(ENTRY_COUNT)};
  std::memcpy(output.data(), magic, sizeof(magic));
  output[6] = static_cast<uint8_t>(from.size());
  output[7] = static_cast<uint8_t>(from.size() >> 8);
  output[8] = static_cast<uint8_t>(to.size());
  output[9] = static_cast<uint8_t>(to.size() >> 8);
  put64(output.data() + 10, id);
  size_t offset = HEADER_BYTES;
  for (const auto& entry : entries) {
    output[offset++] = static_cast<uint8_t>(entry.kind);
    for (size_t i = 0; i < 4; ++i) output[offset++] = static_cast<uint8_t>(entry.firstCluster >> (8 * i));
    put64(output.data() + offset, entry.size);
    offset += 8;
    put64(output.data() + offset, entry.checksum);
    offset += 8;
  }
  std::memcpy(output.data() + offset, from.data(), from.size());
  offset += from.size();
  std::memcpy(output.data() + offset, to.data(), to.size());
  offset += to.size();
  put64(output.data() + offset, readingstats::completionjournal::checksum(output.data(), offset));
  size = offset + CHECKSUM_BYTES;
  return true;
}

// Views borrow the validated record. Output values change only on success.
inline bool decodePlan(const uint8_t* bytes, size_t size, std::string_view& from, std::string_view& to, uint64_t& id,
                       Identities* identities = nullptr) {
  constexpr uint8_t magic[] = {'C', 'P', 'B', 'M', 1, static_cast<uint8_t>(ENTRY_COUNT)};
  constexpr size_t pathsOffset = HEADER_BYTES + ENTRY_COUNT * ENTRY_BYTES;
  if (!bytes || size < pathsOffset + CHECKSUM_BYTES || size > MAX_PLAN_BYTES ||
      std::memcmp(bytes, magic, sizeof(magic)) != 0)
    return false;
  const size_t fromSize = bytes[6] | (size_t{bytes[7]} << 8);
  const size_t toSize = bytes[8] | (size_t{bytes[9]} << 8);
  if (fromSize > MAX_PATH_BYTES || toSize > MAX_PATH_BYTES ||
      size != pathsOffset + fromSize + toSize + CHECKSUM_BYTES ||
      get64(bytes + size - CHECKSUM_BYTES) != readingstats::completionjournal::checksum(bytes, size - CHECKSUM_BYTES))
    return false;
  const std::string_view source(reinterpret_cast<const char*>(bytes + pathsOffset), fromSize);
  const std::string_view target(reinterpret_cast<const char*>(bytes + pathsOffset + fromSize), toSize);
  const uint64_t transaction = get64(bytes + 10);
  if (!transaction || !validMovePath(source) || !validMovePath(target) || source == target) return false;
  for (size_t offset = HEADER_BYTES; offset < pathsOffset; offset += ENTRY_BYTES) {
    EntryIdentity entry;
    entry.kind = static_cast<EntryKind>(bytes[offset]);
    for (size_t i = 0; i < 4; ++i) entry.firstCluster |= uint32_t{bytes[offset + 1 + i]} << (8 * i);
    entry.size = get64(bytes + offset + 5);
    entry.checksum = get64(bytes + offset + 13);
    const size_t index = (offset - HEADER_BYTES) / ENTRY_BYTES;
    if (!validIdentity(entry) || (entry.kind != EntryKind::Missing && entry.kind != expectedKind(index)) ||
        (index == 0 && entry.kind != EntryKind::File))
      return false;
  }
  if (identities) {
    for (size_t i = 0, offset = HEADER_BYTES; i < ENTRY_COUNT; ++i, offset += ENTRY_BYTES) {
      auto& entry = (*identities)[i];
      entry = {};
      entry.kind = static_cast<EntryKind>(bytes[offset]);
      for (size_t j = 0; j < 4; ++j) entry.firstCluster |= uint32_t{bytes[offset + 1 + j]} << (8 * j);
      entry.size = get64(bytes + offset + 5);
      entry.checksum = get64(bytes + offset + 13);
    }
  }
  from = source;
  to = target;
  id = transaction;
  return true;
}
}  // namespace bookmove
