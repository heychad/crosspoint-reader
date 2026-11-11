#pragma once

// Test-only serialization experiment. The volume token is supplied by the
// caller; this codec does not establish SD-card identity or authenticate data.
#include <CompletionJournal.h>
#include <FsLib/FsFile.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace aliasrecord {
using VolumeToken = std::array<uint8_t, 16>;
constexpr size_t RECORD_BYTES = 8 + 16 + 8 + 4 + 4 + 4 + 2 + 1 + 1 + 21 * 32 + 8;
using Record = std::array<uint8_t, RECORD_BYTES>;
using Seal = std::array<uint8_t, 24>;

inline void put(uint8_t* bytes, size_t& offset, uint64_t value, size_t count) {
  for (size_t i = 0; i < count; ++i) bytes[offset++] = static_cast<uint8_t>(value >> (8 * i));
}
inline uint64_t get(const uint8_t* bytes, size_t& offset, size_t count) {
  uint64_t value = 0;
  for (size_t i = 0; i < count; ++i) value |= uint64_t{bytes[offset++]} << (8 * i);
  return value;
}
inline bool encode(const FsFile::ExperimentLocator& locator, const VolumeToken& volume, uint64_t transaction,
                   Record& output) {
  if (!locator.parentCluster || locator.firstCluster < 2 || !locator.fileSize || locator.lfnCount > 20 ||
      locator.directoryIndex < locator.lfnCount)
    return false;
  Record candidate{};
  constexpr uint8_t magic[8] = {'X', '4', 'A', 'L', 'I', 'A', 'S', 1};
  size_t offset = 0;
  std::memcpy(candidate.data(), magic, sizeof(magic));
  offset += sizeof(magic);
  std::memcpy(candidate.data() + offset, volume.data(), volume.size());
  offset += volume.size();
  put(candidate.data(), offset, transaction, 8);
  put(candidate.data(), offset, locator.parentCluster, 4);
  put(candidate.data(), offset, locator.firstCluster, 4);
  put(candidate.data(), offset, locator.fileSize, 4);
  put(candidate.data(), offset, locator.directoryIndex, 2);
  put(candidate.data(), offset, locator.lfnCount, 1);
  put(candidate.data(), offset, 0, 1);
  for (size_t i = 0; i <= locator.lfnCount; ++i)
    std::memcpy(candidate.data() + offset + i * 32, locator.entries[i], 32);
  offset += 21 * 32;
  put(candidate.data(), offset, readingstats::completionjournal::checksum(candidate.data(), offset), 8);
  if (offset != candidate.size()) return false;
  output = candidate;
  return true;
}

inline bool decode(const Record& bytes, const VolumeToken& volume, uint64_t transaction,
                   FsFile::ExperimentLocator& output) {
  constexpr uint8_t magic[8] = {'X', '4', 'A', 'L', 'I', 'A', 'S', 1};
  if (std::memcmp(bytes.data(), magic, sizeof(magic)) || std::memcmp(bytes.data() + 8, volume.data(), volume.size()))
    return false;
  size_t hashOffset = bytes.size() - 8;
  size_t hashRead = hashOffset;
  if (get(bytes.data(), hashRead, 8) != readingstats::completionjournal::checksum(bytes.data(), hashOffset))
    return false;
  size_t offset = 24;
  if (get(bytes.data(), offset, 8) != transaction) return false;
  FsFile::ExperimentLocator candidate{};
  candidate.parentCluster = static_cast<uint32_t>(get(bytes.data(), offset, 4));
  candidate.firstCluster = static_cast<uint32_t>(get(bytes.data(), offset, 4));
  candidate.fileSize = static_cast<uint32_t>(get(bytes.data(), offset, 4));
  candidate.directoryIndex = static_cast<uint16_t>(get(bytes.data(), offset, 2));
  candidate.lfnCount = static_cast<uint8_t>(get(bytes.data(), offset, 1));
  if (get(bytes.data(), offset, 1) != 0 || !candidate.parentCluster || candidate.firstCluster < 2 ||
      !candidate.fileSize || candidate.lfnCount > 20 || candidate.directoryIndex < candidate.lfnCount)
    return false;
  std::memcpy(candidate.entries, bytes.data() + offset, 21 * 32);
  for (size_t i = candidate.lfnCount + 1; i < 21; ++i)
    for (uint8_t value : candidate.entries[i])
      if (value) return false;
  output = candidate;
  return true;
}

inline Seal seal(const Record& record, uint64_t transaction) {
  Seal result{'X', '4', 'A', 'S', 1, 0, 0, 0};
  size_t offset = 8;
  put(result.data(), offset, readingstats::completionjournal::checksum(record.data(), record.size()), 8);
  put(result.data(), offset, transaction, 8);
  return result;
}
}  // namespace aliasrecord
