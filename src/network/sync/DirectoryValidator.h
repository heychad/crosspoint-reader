#pragma once

#include <cstddef>
#include <cstdint>

// One 32-byte directory record per call. No I/O, allocation or locking.
// A caller must stop on Error and must separately verify read/close errors,
// directory identity, concurrent mutation and complete enumeration.
namespace wirecheck {
enum class Format { Fat, ExFat };
enum class Result { More, End, Error };
class DirectoryValidator {
 public:
  explicit DirectoryValidator(Format format) : format_(format) {}
  Result accept(const uint8_t* entry, size_t size) {
    if (!entry || size != 32 || state_ != Result::More || ++count_ > 4096) return fail();
    return format_ == Format::Fat ? fat(entry) : exfat(entry);
  }
  Result finish() {
    if (state_ == Result::Error || pending_ != 0) return fail();
    state_ = Result::End;
    return state_;
  }

 private:
  Result fail() { return state_ = Result::Error; }
  static uint8_t fatChecksum(const uint8_t* name) {
    uint8_t sum = 0;
    for (unsigned i = 0; i < 11; ++i) sum = static_cast<uint8_t>((sum >> 1) + (sum << 7) + name[i]);
    return sum;
  }
  void checksum(const uint8_t* entry, bool primary) {
    for (unsigned i = 0; i < 32; ++i) {
      if (primary && (i == 2 || i == 3)) continue;
      exfatChecksum_ =
          static_cast<uint16_t>((static_cast<uint32_t>(exfatChecksum_) << 15 | exfatChecksum_ >> 1) + entry[i]);
    }
  }
  Result fat(const uint8_t* entry) {
    const bool active = entry[0] != 0 && entry[0] != 0xe5;
    if (active && entry[11] == 0x0f) {
      const uint8_t ordinal = entry[0] & 0x1f;
      const bool last = (entry[0] & 0x40) != 0;
      if ((entry[0] & ~(0x40 | 0x1f)) || !ordinal || ordinal > 20 ||
          (last ? pending_ != 0 : pending_ <= 1 || ordinal + 1 != pending_ || entry[13] != fatChecksum_))
        return fail();
      if (last) fatChecksum_ = entry[13];
      pending_ = ordinal;
    } else if (pending_) {
      if (!active || pending_ != 1 || (entry[11] & 0x08) || fatChecksum(entry) != fatChecksum_) return fail();
      pending_ = 0;
    }
    if (entry[0] == 0) return state_ = Result::End;
    return Result::More;
  }
  Result exfat(const uint8_t* entry) {
    const uint8_t type = entry[0];
    if (pending_) {
      const unsigned index = total_ - pending_;
      if (index == 0) {
        if (type != 0xc0 || entry[3] == 0) return fail();
        names_ = (entry[3] + 14) / 15;
        if (static_cast<unsigned>(names_) + 1 > total_) return fail();
      } else if (index <= names_) {
        if (type != 0xc1) return fail();
      } else if ((type & 0xe0) != 0xe0) {
        return fail();
      }
      checksum(entry, false);
      if (--pending_ == 0 && exfatChecksum_ != expected_) return fail();
      return Result::More;
    }
    if (type == 0) return state_ = Result::End;
    if (!(type & 0x80) || type == 0x81 || type == 0x82 || type == 0x83) return Result::More;
    if (type != 0x85 || entry[1] < 2 || entry[1] > 32 || count_ + entry[1] > 4096) return fail();
    pending_ = total_ = entry[1];
    names_ = 0;
    expected_ = static_cast<uint16_t>(entry[2] | (static_cast<uint16_t>(entry[3]) << 8));
    exfatChecksum_ = 0;
    checksum(entry, true);
    return Result::More;
  }
  Format format_;
  Result state_ = Result::More;
  unsigned count_ = 0;
  uint8_t pending_ = 0, total_ = 0, names_ = 0, fatChecksum_ = 0;
  uint16_t expected_ = 0, exfatChecksum_ = 0;
};
}  // namespace wirecheck
