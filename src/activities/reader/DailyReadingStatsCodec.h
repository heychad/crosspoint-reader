#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dailyreading {
// CrossInk v2.0.0-rc monthly snapshots: header, 31 counters, FNV-1a checksum.
constexpr size_t MONTH_BYTES = 264;
constexpr size_t LEGACY_BYTES = 13;
struct Counter {
  uint32_t seconds = 0;
  uint32_t uploaded = 0;
  bool operator==(const Counter&) const = default;
};
struct Month {
  std::array<Counter, 31> days{};
  uint32_t generation = 0;
  bool operator==(const Month&) const = default;
};
enum class Decode : uint8_t { Missing, Valid, Corrupt, Unsupported, Unreadable };
enum class Selection : uint8_t { Empty, First, Second, Unavailable };

inline uint32_t read32(const uint8_t* bytes) {
  return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 | uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
}
inline void write32(uint8_t* bytes, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) bytes[i] = value >> (8 * i);
}
inline uint32_t checksum(const uint8_t* bytes, size_t size, uint32_t seed = 2166136261U) {
  for (size_t i = 0; i < size; ++i) seed = (seed ^ bytes[i]) * 16777619U;
  return seed;
}
inline bool valid(const Counter& counter) { return counter.uploaded <= counter.seconds; }

// The caller validates the calendar month and supplies its first day and length.
inline Decode decodeMonth(const uint8_t* bytes, size_t size, uint32_t firstDay, uint8_t dayCount, Month& result) {
  result = {};
  if (!bytes || dayCount < 28 || dayCount > 31) return Decode::Corrupt;
  if (size > MONTH_BYTES || (size && bytes[0] > 2)) return Decode::Unsupported;
  if (size != MONTH_BYTES || bytes[0] != 2 || bytes[1] != 'D' || bytes[2] != 'R' || bytes[3] != 0 ||
      read32(bytes + 4) != firstDay || read32(bytes + MONTH_BYTES - 4) != checksum(bytes, MONTH_BYTES - 4))
    return Decode::Corrupt;
  for (size_t i = 0; i < result.days.size(); ++i) {
    const Counter counter{read32(bytes + 12 + i * 8), read32(bytes + 16 + i * 8)};
    if (!valid(counter) || (i >= dayCount && (counter.seconds || counter.uploaded))) return Decode::Corrupt;
  }
  result.generation = read32(bytes + 8);
  for (size_t i = 0; i < result.days.size(); ++i)
    result.days[i] = {read32(bytes + 12 + i * 8), read32(bytes + 16 + i * 8)};
  return Decode::Valid;
}

inline bool encodeMonth(const Month& month, uint32_t firstDay, uint8_t dayCount,
                        std::array<uint8_t, MONTH_BYTES>& bytes) {
  if (dayCount < 28 || dayCount > 31) return false;
  for (size_t i = 0; i < month.days.size(); ++i)
    if (!valid(month.days[i]) || (i >= dayCount && (month.days[i].seconds || month.days[i].uploaded))) return false;
  bytes.fill(0);
  bytes[0] = 2;
  bytes[1] = 'D';
  bytes[2] = 'R';
  write32(bytes.data() + 4, firstDay);
  write32(bytes.data() + 8, month.generation);
  for (size_t i = 0; i < month.days.size(); ++i) {
    write32(bytes.data() + 12 + i * 8, month.days[i].seconds);
    write32(bytes.data() + 16 + i * 8, month.days[i].uploaded);
  }
  write32(bytes.data() + MONTH_BYTES - 4, checksum(bytes.data(), MONTH_BYTES - 4));
  return true;
}

inline Decode decodeLegacy(const uint8_t* bytes, size_t size, uint32_t day, Counter& result) {
  result = {};
  if (!bytes) return Decode::Corrupt;
  if (size > LEGACY_BYTES || (size && bytes[0] > 1)) return Decode::Unsupported;
  if (size != LEGACY_BYTES || bytes[0] != 1 || read32(bytes + 9) != checksum(bytes, 9, 2166136261U ^ day))
    return Decode::Corrupt;
  const Counter counter{read32(bytes + 1), read32(bytes + 5)};
  if (!valid(counter)) return Decode::Corrupt;
  result = counter;
  return Decode::Valid;
}

inline Selection select(Decode firstStatus, const Month& first, Decode secondStatus, const Month& second) {
  if (firstStatus == Decode::Unsupported || secondStatus == Decode::Unsupported || firstStatus == Decode::Unreadable ||
      secondStatus == Decode::Unreadable)
    return Selection::Unavailable;
  if (firstStatus == Decode::Missing && secondStatus == Decode::Missing) return Selection::Empty;
  if (firstStatus != Decode::Valid) return secondStatus == Decode::Valid ? Selection::Second : Selection::Unavailable;
  if (secondStatus != Decode::Valid) return Selection::First;
  const uint32_t delta = second.generation - first.generation;
  if (!delta) return first == second ? Selection::First : Selection::Unavailable;
  if (delta == 0x80000000U) return Selection::Unavailable;
  return delta < 0x80000000U ? Selection::Second : Selection::First;
}
}  // namespace dailyreading
