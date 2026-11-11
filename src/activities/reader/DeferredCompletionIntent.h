#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "CompletionMovePlan.h"

// A deferred intent records the source, not the files that may change while
// the book is open. It does not publish an intent or authorize a move.
namespace readingstats::deferredcompletion {
constexpr size_t HEADER_BYTES = 16;
constexpr size_t HASH_BYTES = 8;
constexpr size_t MAX_RECORD = HEADER_BYTES + completionmove::MAX_PATH_BYTES + HASH_BYTES;
using Record = std::array<uint8_t, MAX_RECORD>;

inline bool encode(std::string_view source, uint64_t completionHash, Record& output, size_t& size) {
  size = 0;
  if (!completionmove::validPath(source)) return false;
  constexpr std::array<uint8_t, 6> magic{'C', 'P', 'D', 'I', 1, 0};
  std::memcpy(output.data(), magic.data(), magic.size());
  output[6] = static_cast<uint8_t>(source.size());
  output[7] = static_cast<uint8_t>(source.size() >> 8);
  for (size_t i = 0; i < 8; ++i) output[8 + i] = static_cast<uint8_t>(completionHash >> (8 * i));
  std::memcpy(output.data() + HEADER_BYTES, source.data(), source.size());
  const size_t hashOffset = HEADER_BYTES + source.size();
  const uint64_t hash = completionjournal::checksum(output.data(), hashOffset);
  for (size_t i = 0; i < HASH_BYTES; ++i) output[hashOffset + i] = static_cast<uint8_t>(hash >> (8 * i));
  size = hashOffset + HASH_BYTES;
  return true;
}

// The source borrows the record and changes only on successful decoding.
inline bool decode(const uint8_t* bytes, size_t size, uint64_t completionHash, std::string_view& source) {
  constexpr std::array<uint8_t, 6> magic{'C', 'P', 'D', 'I', 1, 0};
  if (!bytes || size < HEADER_BYTES + HASH_BYTES || size > MAX_RECORD ||
      std::memcmp(bytes, magic.data(), magic.size()) != 0)
    return false;
  const size_t pathSize = bytes[6] | (static_cast<size_t>(bytes[7]) << 8);
  if (pathSize > completionmove::MAX_PATH_BYTES || size != HEADER_BYTES + pathSize + HASH_BYTES) return false;
  for (size_t i = 0; i < 8; ++i)
    if (bytes[8 + i] != static_cast<uint8_t>(completionHash >> (8 * i))) return false;
  uint64_t stored = 0;
  for (size_t i = 0; i < HASH_BYTES; ++i) stored |= uint64_t{bytes[size - HASH_BYTES + i]} << (8 * i);
  if (stored != completionjournal::checksum(bytes, size - HASH_BYTES)) return false;
  const std::string_view candidate(reinterpret_cast<const char*>(bytes + HEADER_BYTES), pathSize);
  if (!completionmove::validPath(candidate)) return false;
  source = candidate;
  return true;
}

inline bool decodeWithHash(const uint8_t* bytes, size_t size, uint64_t& completionHash, std::string_view& source) {
  if (!bytes || size < HEADER_BYTES + HASH_BYTES || size > MAX_RECORD) return false;
  uint64_t candidateHash = 0;
  for (size_t i = 0; i < 8; ++i) candidateHash |= uint64_t{bytes[8 + i]} << (8 * i);
  std::string_view candidateSource;
  if (!decode(bytes, size, candidateHash, candidateSource)) return false;
  completionHash = candidateHash;
  source = candidateSource;
  return true;
}

using Seal = std::array<uint8_t, 16>;
inline bool encodeSeal(unsigned slot, const Record& record, size_t size, Seal& seal, bool retired = false) {
  uint64_t completionHash = 0;
  std::string_view source;
  if (slot >= 64 || !decodeWithHash(record.data(), size, completionHash, source)) return false;
  Seal candidate{'C',
                 'P',
                 'D',
                 static_cast<uint8_t>(retired ? 'R' : 'S'),
                 1,
                 static_cast<uint8_t>(slot),
                 static_cast<uint8_t>(size),
                 static_cast<uint8_t>(size >> 8)};
  const uint64_t hash = completionjournal::checksum(record.data(), size);
  for (size_t i = 0; i < 8; ++i) candidate[8 + i] = static_cast<uint8_t>(hash >> (8 * i));
  seal = candidate;
  return true;
}
}  // namespace readingstats::deferredcompletion
