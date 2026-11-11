#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

// A single immutable record for a pending book/global statistics update.
// This codec does not publish files, recover transactions, or establish ownership.
namespace readingstats::completionjournal {
constexpr size_t BOOK_BYTES = 73;
constexpr size_t GLOBAL_BYTES = 159;
constexpr size_t MAX_PATH = 127;
constexpr size_t HEADER_BYTES = 10;
constexpr size_t DEFERRED_HEADER_BYTES = 12;
constexpr size_t DEFERRED_MOVE_HEADER_BYTES = 20;
constexpr size_t MAX_SOURCE_PATH = 1023;
constexpr size_t HASH_BYTES = 8;
constexpr uint8_t REMOVE_RECENTS = 1;
constexpr uint8_t MOVE_TO_READ = 2;
constexpr uint8_t DEFER_MOVE_TO_READ = 4;
constexpr uint8_t MOVE_FROM_DEFERRED = 8;
constexpr uint8_t POLICY_MASK = REMOVE_RECENTS | MOVE_TO_READ | DEFER_MOVE_TO_READ | MOVE_FROM_DEFERRED;
constexpr size_t MAX_RECORD =
    DEFERRED_MOVE_HEADER_BYTES + MAX_PATH + 2 * BOOK_BYTES + 2 * GLOBAL_BYTES + MAX_SOURCE_PATH + HASH_BYTES;
using Book = std::array<uint8_t, BOOK_BYTES>;
using Global = std::array<uint8_t, GLOBAL_BYTES>;
// Size zero means the corresponding canonical file was definitely absent.
// Existing files must be captured byte-for-byte while the transaction is locked.
struct Baseline {
  Book book{};
  Global global{};
  uint8_t bookSize = 0;
  uint8_t globalSize = 0;
};
using Record = std::array<uint8_t, MAX_RECORD>;
using Seal = std::array<uint8_t, 16>;

inline bool validDirectory(std::string_view directory) {
  constexpr std::string_view prefix = "/.crosspoint/reading_stats/";
  if (!directory.starts_with(prefix) || directory.size() <= prefix.size() || directory.size() > MAX_PATH) return false;
  for (const char c : directory.substr(prefix.size())) {
    if ((c < '0' || c > '9') && (c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && c != '_' && c != '-') return false;
  }
  return true;
}

inline uint64_t checksum(const uint8_t* bytes, size_t size) {
  uint64_t value = 14695981039346656037ULL;
  for (size_t i = 0; i < size; ++i) value = (value ^ bytes[i]) * 1099511628211ULL;
  return value;
}

inline bool validSourcePath(std::string_view path) {
  if (path.size() < 2 || path.size() > MAX_SOURCE_PATH || path.front() != '/' || path.back() == '/') return false;
  size_t start = 1;
  while (start < path.size()) {
    const size_t slash = path.find('/', start);
    const auto component = path.substr(start, slash == std::string_view::npos ? path.size() - start : slash - start);
    if (component.empty() || component == "." || component == ".." || component.find('\\') != std::string_view::npos)
      return false;
    for (const unsigned char c : component)
      if (c < 0x20) return false;
    if (slash == std::string_view::npos) break;
    start = slash + 1;
  }
  return true;
}

inline bool encode(std::string_view directory, const Book& book, const Global& global, const Baseline& baseline,
                   Record& output, size_t& size, uint8_t policy = 0, std::string_view deferredSource = {},
                   uint64_t originHash = 0) {
  size = 0;
  const bool deferred = (policy & DEFER_MOVE_TO_READ) != 0;
  const bool deferredMove = (policy & MOVE_FROM_DEFERRED) != 0;
  const bool hasSource = deferred || deferredMove;
  if (!validDirectory(directory) || baseline.bookSize > BOOK_BYTES || baseline.globalSize > GLOBAL_BYTES ||
      (policy & ~POLICY_MASK) || (deferred && (policy & MOVE_TO_READ)) ||
      (deferredMove && (!(policy & MOVE_TO_READ) || deferred)) || (!deferredMove && originHash != 0) ||
      (hasSource ? !validSourcePath(deferredSource) : !deferredSource.empty()))
    return false;
  output.fill(0);
  const std::array<uint8_t, 6> magic{'C', 'P', 'C', 'J', static_cast<uint8_t>(deferredMove ? 5 : deferred ? 4 : 3), 0};
  std::memcpy(output.data(), magic.data(), magic.size());
  output[6] = static_cast<uint8_t>(directory.size());
  output[7] = baseline.bookSize;
  output[8] = baseline.globalSize;
  output[9] = policy;
  if (hasSource) {
    output[10] = static_cast<uint8_t>(deferredSource.size());
    output[11] = static_cast<uint8_t>(deferredSource.size() >> 8);
  }
  if (deferredMove)
    for (size_t i = 0; i < 8; ++i) output[12 + i] = static_cast<uint8_t>(originHash >> (8 * i));
  size_t offset = deferredMove ? DEFERRED_MOVE_HEADER_BYTES : deferred ? DEFERRED_HEADER_BYTES : HEADER_BYTES;
  std::memcpy(output.data() + offset, directory.data(), directory.size());
  offset += directory.size();
  std::memcpy(output.data() + offset, book.data(), book.size());
  offset += book.size();
  std::memcpy(output.data() + offset, global.data(), global.size());
  offset += global.size();
  std::memcpy(output.data() + offset, baseline.book.data(), baseline.bookSize);
  offset += baseline.bookSize;
  std::memcpy(output.data() + offset, baseline.global.data(), baseline.globalSize);
  offset += baseline.globalSize;
  if (hasSource) {
    std::memcpy(output.data() + offset, deferredSource.data(), deferredSource.size());
    offset += deferredSource.size();
  }
  const auto hash = checksum(output.data(), offset);
  for (size_t i = 0; i < HASH_BYTES; ++i) output[offset + i] = static_cast<uint8_t>(hash >> (8 * i));
  size = offset + HASH_BYTES;
  return true;
}

// Views point into the supplied immutable record and are valid only as long as it is unchanged.
inline bool decode(const uint8_t* bytes, size_t size, std::string_view& directory, const uint8_t*& book,
                   const uint8_t*& global) {
  if (!bytes || size < HEADER_BYTES + BOOK_BYTES + GLOBAL_BYTES + HASH_BYTES || size > MAX_RECORD) return false;
  constexpr std::array<uint8_t, 4> magic{'C', 'P', 'C', 'J'};
  if (std::memcmp(bytes, magic.data(), magic.size()) != 0 || (bytes[4] < 3 || bytes[4] > 5) || bytes[5] != 0 ||
      (bytes[9] & ~POLICY_MASK) || bytes[7] > BOOK_BYTES || bytes[8] > GLOBAL_BYTES)
    return false;
  const bool deferred = bytes[4] == 4;
  const bool deferredMove = bytes[4] == 5;
  const bool hasSource = deferred || deferredMove;
  if (deferred != static_cast<bool>(bytes[9] & DEFER_MOVE_TO_READ) ||
      deferredMove != static_cast<bool>(bytes[9] & MOVE_FROM_DEFERRED) || (deferred && (bytes[9] & MOVE_TO_READ)) ||
      (deferredMove && !(bytes[9] & MOVE_TO_READ)))
    return false;
  const size_t headerSize = deferredMove ? DEFERRED_MOVE_HEADER_BYTES : deferred ? DEFERRED_HEADER_BYTES : HEADER_BYTES;
  if (size < headerSize + BOOK_BYTES + GLOBAL_BYTES + HASH_BYTES) return false;
  const size_t sourceSize = hasSource ? (bytes[10] | (static_cast<size_t>(bytes[11]) << 8)) : 0;
  const size_t pathSize = bytes[6];
  if (sourceSize > MAX_SOURCE_PATH ||
      size != headerSize + pathSize + BOOK_BYTES + GLOBAL_BYTES + bytes[7] + bytes[8] + sourceSize + HASH_BYTES)
    return false;
  const std::string_view candidate(reinterpret_cast<const char*>(bytes + headerSize), pathSize);
  if (!validDirectory(candidate)) return false;
  if (hasSource && !validSourcePath(std::string_view(
                       reinterpret_cast<const char*>(bytes + size - HASH_BYTES - sourceSize), sourceSize)))
    return false;
  uint64_t stored = 0;
  for (size_t i = 0; i < HASH_BYTES; ++i) stored |= static_cast<uint64_t>(bytes[size - HASH_BYTES + i]) << (8 * i);
  if (stored != checksum(bytes, size - HASH_BYTES)) return false;
  directory = candidate;
  book = bytes + headerSize + pathSize;
  global = book + BOOK_BYTES;
  return true;
}

// Version 5 binds the move to a deferred request's original completion hash.
inline bool decodeDeferredMove(const uint8_t* bytes, size_t size, std::string_view& source, uint64_t& originHash) {
  std::string_view directory;
  const uint8_t *book = nullptr, *global = nullptr;
  if (!decode(bytes, size, directory, book, global) || bytes[4] != 5) return false;
  const size_t sourceSize = bytes[10] | (static_cast<size_t>(bytes[11]) << 8);
  uint64_t hash = 0;
  for (size_t i = 0; i < 8; ++i) hash |= uint64_t{bytes[12 + i]} << (8 * i);
  source = std::string_view(reinterpret_cast<const char*>(bytes + size - HASH_BYTES - sourceSize), sourceSize);
  originHash = hash;
  return true;
}

// On success, the view borrows the immutable record. Version 3 has no source.
inline bool decodeDeferredSource(const uint8_t* bytes, size_t size, std::string_view& source) {
  std::string_view directory;
  const uint8_t *book = nullptr, *global = nullptr;
  if (!decode(bytes, size, directory, book, global) || bytes[4] != 4) return false;
  const size_t sourceSize = bytes[10] | (static_cast<size_t>(bytes[11]) << 8);
  source = std::string_view(reinterpret_cast<const char*>(bytes + size - HASH_BYTES - sourceSize), sourceSize);
  return true;
}

// Returned pointers borrow the record; a zero size means the file was absent.
inline bool decodeBaseline(const uint8_t* bytes, size_t size, const uint8_t*& oldBook, size_t& oldBookSize,
                           const uint8_t*& oldGlobal, size_t& oldGlobalSize) {
  std::string_view directory;
  const uint8_t *book = nullptr, *global = nullptr;
  if (!decode(bytes, size, directory, book, global)) return false;
  oldBook = global + GLOBAL_BYTES;
  oldBookSize = bytes[7];
  oldGlobal = oldBook + oldBookSize;
  oldGlobalSize = bytes[8];
  return true;
}

// A seal binds a slot to one complete record. It is not proof that either file
// was durably committed, nor does it validate the embedded statistics payloads.
inline bool encodeSeal(unsigned slot, const Record& record, size_t size, Seal& output) {
  std::string_view directory;
  const uint8_t *book = nullptr, *global = nullptr;
  if (slot >= 64 || !decode(record.data(), size, directory, book, global)) return false;
  Seal candidate{
      'C', 'P', 'C', 'S', 1, static_cast<uint8_t>(slot), static_cast<uint8_t>(size), static_cast<uint8_t>(size >> 8)};
  const uint64_t hash = checksum(record.data(), size);
  for (size_t i = 0; i < HASH_BYTES; ++i) candidate[8 + i] = static_cast<uint8_t>(hash >> (8 * i));
  output = candidate;
  return true;
}

inline bool matchesSeal(unsigned slot, const Record& record, size_t size, const Seal& seal) {
  Seal expected{};
  return encodeSeal(slot, record, size, expected) && expected == seal;
}

inline bool encodeRetirement(unsigned slot, const Record& record, size_t size, Seal& output) {
  Seal candidate{};
  if (!encodeSeal(slot, record, size, candidate)) return false;
  candidate[3] = 'R';
  output = candidate;
  return true;
}

inline bool matchesRetirement(unsigned slot, const Record& record, size_t size, const Seal& retirement) {
  Seal expected{};
  return encodeRetirement(slot, record, size, expected) && expected == retirement;
}
}  // namespace readingstats::completionjournal
