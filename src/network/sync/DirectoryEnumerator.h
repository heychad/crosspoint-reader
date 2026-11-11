#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "DirectoryValidator.h"

namespace wirecheck {
struct DirectoryEntry {
  char name[766]{};  // 255 UTF-16 code units, in UTF-8, plus NUL.
  uint64_t size = 0;
  bool directory = false;
};
enum class Step { More, Entry, End, Error };

// Isolated prototype. The caller provides one raw directory record per step,
// keeps this object alive, and must serialize it against directory mutations.
class DirectoryEnumerator {
 public:
  explicit DirectoryEnumerator(Format format) : format_(format), validator_(format) { resetName(); }
  Step accept(const uint8_t* entry, size_t size, DirectoryEntry& out) {
    if (failed_ || ended_) return Step::Error;
    const auto state = validator_.accept(entry, size);
    if (state == Result::Error) {
      failed_ = true;
      return Step::Error;
    }
    if (format_ == Format::Fat && seenDot_ && records_ == 1 &&
        (state != Result::More || entry[0] != '.' || entry[1] != '.')) {
      failed_ = true;
      return Step::Error;
    }
    if (state == Result::End) {
      ended_ = true;
      return Step::End;
    }
    ++records_;
    const auto step = format_ == Format::Fat ? fat(entry, out) : exfat(entry, out);
    if (step == Step::Error) failed_ = true;
    return step;
  }
  Step finish() {
    if (failed_) return Step::Error;
    if (format_ == Format::Fat && seenDot_ && records_ == 1) {
      failed_ = true;
      return Step::Error;
    }
    ended_ = true;
    return validator_.finish() == Result::End ? Step::End : Step::Error;
  }

 private:
  static uint16_t u16(const uint8_t* p) { return p[0] | (static_cast<uint16_t>(p[1]) << 8); }
  static uint64_t u64(const uint8_t* p) {
    uint64_t result = 0;
    for (unsigned i = 0; i < 8; ++i) result |= static_cast<uint64_t>(p[i]) << (8 * i);
    return result;
  }
  void resetName() {
    for (auto& value : chars_) value = 0xffff;
  }
  static bool put(char* output, size_t& pos, uint32_t value) {
    if (value == 0 || value == '/' || value == '\\' || value == '"' || value == '*' || value == ':' || value == '<' ||
        value == '>' || value == '?' || value == '|' || value < 0x20)
      return false;
    unsigned count = value < 0x80 ? 1 : value < 0x800 ? 2 : value < 0x10000 ? 3 : 4;
    if (pos + count >= 766) return false;
    if (count == 1)
      output[pos++] = static_cast<char>(value);
    else {
      const unsigned leading = count == 2 ? 0xc0 : count == 3 ? 0xe0 : 0xf0;
      output[pos++] = static_cast<char>(leading | (value >> (6 * (count - 1))));
      for (unsigned shift = count - 1; shift > 0; --shift)
        output[pos++] = static_cast<char>(0x80 | ((value >> (6 * (shift - 1))) & 0x3f));
    }
    return true;
  }
  bool name(DirectoryEntry& out, size_t count, bool explicitLength) const {
    if (count > 255) return false;
    size_t pos = 0;
    bool ended = false;
    for (size_t i = 0; i < count; ++i) {
      uint32_t value = chars_[i];
      if (!explicitLength && value == 0) {
        if (ended) return false;
        ended = true;
        continue;
      }
      if (!explicitLength && value == 0xffff) {
        if (!ended) return false;
        continue;
      }
      if (ended || value == 0 || value == 0xffff) return false;
      if (value >= 0xd800 && value <= 0xdbff) {
        if (i + 1 >= count) return false;
        const uint32_t next = chars_[++i];
        if (next < 0xdc00 || next > 0xdfff) return false;
        value = 0x10000 + ((value - 0xd800) << 10) + (next - 0xdc00);
      } else if (value >= 0xdc00 && value <= 0xdfff)
        return false;
      if (!put(out.name, pos, value)) return false;
    }
    if (!pos) return false;
    out.name[pos] = '\0';
    // SdFat trims leading spaces and trailing spaces/dots when opening a path.
    // Such names could otherwise resolve to a different entry than listed.
    return out.name[0] != ' ' && out.name[pos - 1] != ' ' && out.name[pos - 1] != '.' &&
           std::strcmp(out.name, ".") != 0 && std::strcmp(out.name, "..") != 0;
  }
  bool shortName(const uint8_t* entry, DirectoryEntry& out) const {
    size_t pos = 0;
    for (unsigned part = 0; part < 2; ++part) {
      const unsigned begin = part ? 8 : 0, end = part ? 11 : 8;
      unsigned last = end;
      while (last > begin && entry[last - 1] == ' ') --last;
      if (part && last > begin) out.name[pos++] = '.';
      for (unsigned i = begin; i < last; ++i) {
        uint8_t value = entry[i];
        if (value < 0x21 || value >= 0x7f || value == '/' || value == '\\' || value == '.') return false;
        if ((entry[12] & (part ? 0x10 : 0x08)) && value >= 'A' && value <= 'Z') value += 32;
        if (!put(out.name, pos, value)) return false;
      }
    }
    if (!pos) return false;
    out.name[pos] = '\0';
    return true;
  }
  Step fat(const uint8_t* entry, DirectoryEntry& out) {
    if (entry[0] == 0xe5) {
      haveLong_ = false;
      return Step::More;
    }
    if (entry[11] == 0x0f) {
      if (entry[12] != 0 || entry[26] != 0 || entry[27] != 0) return Step::Error;
      const unsigned ordinal = entry[0] & 0x1f;
      if (entry[0] & 0x40) {
        resetName();
        haveLong_ = true;
        longSlots_ = ordinal * 13;
      }
      static constexpr unsigned offsets[] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
      for (unsigned i = 0; i < 13; ++i) chars_[(ordinal - 1) * 13 + i] = u16(entry + offsets[i]);
      return Step::More;
    }
    if (entry[0] == '.') {
      // A FAT subdirectory's dot entries must occupy its first two slots.
      if (haveLong_ || !(entry[11] & 0x10) || (entry[11] & 0x08)) return Step::Error;
      const unsigned start = entry[1] == '.' ? 2 : 1;
      if (records_ != start) return Step::Error;
      if (start == 2 && !seenDot_) return Step::Error;
      for (unsigned i = start; i < 11; ++i)
        if (entry[i] != ' ') return Step::Error;
      if (start == 1) seenDot_ = true;
      return Step::More;
    }
    if (entry[11] & 0x08) {
      haveLong_ = false;
      return Step::More;
    }
    out.name[0] = '\0';
    out.size = 0;
    out.directory = false;
    // The final LFN slot may pad beyond 255 code units. Only zero/FFFF padding
    // is allowed there; a full 255-code-unit name needs no explicit terminator.
    if (haveLong_) {
      for (size_t i = 255; i < longSlots_; ++i)
        if (chars_[i] != 0 && chars_[i] != 0xffff) return Step::Error;
      if (!name(out, longSlots_ > 255 ? 255 : longSlots_, false)) return Step::Error;
    } else if (!shortName(entry, out))
      return Step::Error;
    out.directory = (entry[11] & 0x10) != 0;
    out.size =
        out.directory ? 0 : static_cast<uint32_t>(u16(entry + 28) | (static_cast<uint32_t>(u16(entry + 30)) << 16));
    haveLong_ = false;
    return Step::Entry;
  }
  Step exfat(const uint8_t* entry, DirectoryEntry& out) {
    if (!exPending_) {
      if (entry[0] != 0x85) return Step::More;
      exPending_ = entry[1];
      exTotal_ = entry[1];
      exNameLength_ = 0;
      exNameIndex_ = 0;
      exDirectory_ = (u16(entry + 4) & 0x10) != 0;
      exSize_ = 0;
      resetName();
      return Step::More;
    }
    const unsigned index = exTotal_ - exPending_;
    if (index == 0) {
      exNameLength_ = entry[3];
      exSize_ = u64(entry + 24);
    } else if (entry[0] == 0xc1 && index <= (exNameLength_ + 14) / 15) {
      for (unsigned i = 0; i < 15 && exNameIndex_ < exNameLength_; ++i) chars_[exNameIndex_++] = u16(entry + 2 + i * 2);
    }
    if (--exPending_) return Step::More;
    out.name[0] = '\0';
    out.size = 0;
    out.directory = false;
    if (exNameIndex_ != exNameLength_ || !name(out, exNameLength_, true)) return Step::Error;
    out.directory = exDirectory_;
    out.size = exDirectory_ ? 0 : exSize_;
    return Step::Entry;
  }
  Format format_;
  DirectoryValidator validator_;
  uint16_t chars_[260]{};
  size_t longSlots_ = 0, exNameIndex_ = 0, records_ = 0;
  uint64_t exSize_ = 0;
  uint8_t exPending_ = 0, exTotal_ = 0, exNameLength_ = 0;
  bool haveLong_ = false, exDirectory_ = false, failed_ = false, ended_ = false, seenDot_ = false;
};
}  // namespace wirecheck
