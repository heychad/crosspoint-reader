#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "DirectoryEnumerator.h"
using namespace wirecheck;
using Rec = std::array<uint8_t, 32>;
uint8_t fatSum(const Rec& s) {
  uint8_t r = 0;
  for (int i = 0; i < 11; ++i) r = static_cast<uint8_t>((r >> 1) + (r << 7) + s[i]);
  return r;
}
void put16(Rec& e, int at, uint16_t x) {
  e[at] = x & 255;
  e[at + 1] = x >> 8;
}
std::vector<Rec> fatName(const std::vector<uint16_t>& name) {
  Rec s{};
  std::memcpy(s.data(), "ABCDEF~1EPB", 11);
  s[11] = 0x20;
  std::vector<Rec> result;
  const size_t slots = (name.size() + 12) / 13;
  for (size_t ord = slots; ord > 0; --ord) {
    Rec r{};
    r[0] = ord | (ord == slots ? 0x40 : 0);
    r[11] = 0x0f;
    r[13] = fatSum(s);
    constexpr int offsets[] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
    for (size_t i = 0; i < 13; ++i) {
      size_t index = (ord - 1) * 13 + i;
      put16(r, offsets[i], index < name.size() ? name[index] : index == name.size() ? 0 : 0xffff);
    }
    result.push_back(r);
  }
  result.push_back(s);
  return result;
}
uint16_t exChecksum(const std::vector<Rec>& set) {
  uint16_t sum = 0;
  for (size_t i = 0; i < set.size(); ++i)
    for (int j = 0; j < 32; ++j) {
      if (i == 0 && (j == 2 || j == 3)) continue;
      sum = static_cast<uint16_t>((static_cast<uint32_t>(sum) << 15 | sum >> 1) + set[i][j]);
    }
  return sum;
}
std::vector<Rec> exName(const std::vector<uint16_t>& name) {
  const size_t chunks = (name.size() + 14) / 15;
  std::vector<Rec> set(2 + chunks);
  set[0][0] = 0x85;
  set[0][1] = 1 + chunks;
  set[1][0] = 0xc0;
  set[1][3] = name.size();
  set[1][24] = 0x78;
  set[1][25] = 0x56;
  for (size_t i = 0; i < chunks; ++i) {
    set[i + 2][0] = 0xc1;
    for (size_t j = 0; j < 15 && i * 15 + j < name.size(); ++j) put16(set[i + 2], 2 + j * 2, name[i * 15 + j]);
  }
  put16(set[0], 2, exChecksum(set));
  return set;
}
void check(const std::vector<Rec>& entries, Format format, const std::string& expected, uint64_t size) {
  DirectoryEnumerator decoder(format);
  DirectoryEntry output;
  unsigned emitted = 0;
  for (const auto& rec : entries) {
    auto step = decoder.accept(rec.data(), 32, output);
    assert(step != Step::Error);
    if (step == Step::Entry) {
      ++emitted;
      if (output.name != expected) std::fprintf(stderr, "got %s expected %s\n", output.name, expected.c_str());
      assert(output.name == expected);
      assert(output.size == size);
    }
  }
  assert(emitted == 1);
  assert(decoder.finish() == Step::End);
}
int main() {
  Rec shortEntry{};
  std::memcpy(shortEntry.data(), "HELLO   TXT", 11);
  shortEntry[11] = 0x20;
  shortEntry[12] = 0x18;
  shortEntry[28] = 42;
  check({shortEntry}, Format::Fat, "hello.txt", 42);
  std::vector<uint16_t> unicode = {'B', 'o', 'o', 'k', ' ', 0xd83d, 0xdcda, '.', 'e', 'p', 'u', 'b'};
  check(fatName(unicode), Format::Fat, "Book 📚.epub", 0);
  check(exName(unicode), Format::ExFat, "Book 📚.epub", 0x5678);
  std::vector<uint16_t> max(255, 'a');
  check(fatName(max), Format::Fat, std::string(255, 'a'), 0);
  check(exName(max), Format::ExFat, std::string(255, 'a'), 0x5678);
  auto bad = fatName(unicode);
  bad[0][13] ^= 1;
  DirectoryEnumerator corrupt(Format::Fat);
  DirectoryEntry out;
  bool rejected = false;
  for (auto& r : bad)
    if (corrupt.accept(r.data(), 32, out) == Step::Error) rejected = true;
  assert(rejected);
  auto invalid = exName({0xd800, 'x'});
  DirectoryEnumerator invalidUtf(Format::ExFat);
  rejected = false;
  for (auto& r : invalid)
    if (invalidUtf.accept(r.data(), 32, out) == Step::Error) rejected = true;
  assert(rejected);
  Rec unsupported = shortEntry;
  unsupported[0] = 0x8e;
  DirectoryEnumerator codepage(Format::Fat);
  assert(codepage.accept(unsupported.data(), 32, out) == Step::Error);

  for (const auto& unsafe :
       {std::vector<uint16_t>{' ', 'B'}, std::vector<uint16_t>{'B', ' '}, std::vector<uint16_t>{'B', '.'}}) {
    for (const auto format : {Format::Fat, Format::ExFat}) {
      const auto records = format == Format::Fat ? fatName(unsafe) : exName(unsafe);
      DirectoryEnumerator scanner(format);
      bool failed = false;
      for (const auto& record : records)
        if (scanner.accept(record.data(), record.size(), out) == Step::Error) failed = true;
      assert(failed);
    }
  }
  Rec dot{};
  std::memcpy(dot.data(), ".          ", 11);
  dot[11] = 0x10;
  DirectoryEnumerator dotDirectory(Format::Fat);
  assert(dotDirectory.accept(dot.data(), 32, out) == Step::More);
  DirectoryEnumerator missingSecondDot(Format::Fat);
  assert(missingSecondDot.accept(dot.data(), 32, out) == Step::More);
  assert(missingSecondDot.finish() == Step::Error);
  DirectoryEnumerator fileInsteadOfDot(Format::Fat);
  assert(fileInsteadOfDot.accept(dot.data(), 32, out) == Step::More);
  assert(fileInsteadOfDot.accept(shortEntry.data(), 32, out) == Step::Error);
  DirectoryEnumerator deletedSecondDot(Format::Fat);
  assert(deletedSecondDot.accept(dot.data(), 32, out) == Step::More);
  Rec missing{};
  missing[0] = 0xe5;
  assert(deletedSecondDot.accept(missing.data(), 32, out) == Step::Error);
  DirectoryEnumerator endInsteadOfDot(Format::Fat);
  assert(endInsteadOfDot.accept(dot.data(), 32, out) == Step::More);
  Rec end{};
  assert(endInsteadOfDot.accept(end.data(), 32, out) == Step::Error);
  dot[1] = '.';
  assert(dotDirectory.accept(dot.data(), 32, out) == Step::More);
  assert(dotDirectory.accept(dot.data(), 32, out) == Step::Error);
  DirectoryEnumerator reversedDots(Format::Fat);
  assert(reversedDots.accept(dot.data(), 32, out) == Step::Error);
  DirectoryEnumerator missingFirstDot(Format::Fat);
  assert(missingFirstDot.accept(shortEntry.data(), 32, out) == Step::Entry);
  assert(missingFirstDot.accept(dot.data(), 32, out) == Step::Error);
  Rec deleted{};
  deleted[0] = 0xe5;
  DirectoryEnumerator deletedFirstDot(Format::Fat);
  assert(deletedFirstDot.accept(deleted.data(), 32, out) == Step::More);
  assert(deletedFirstDot.accept(dot.data(), 32, out) == Step::Error);
  Rec malformed = dot;
  malformed[1] = 'B';
  DirectoryEnumerator badDot(Format::Fat);
  assert(badDot.accept(malformed.data(), 32, out) == Step::Error);
  dot[11] = 0x20;
  DirectoryEnumerator dotFile(Format::Fat);
  assert(dotFile.accept(dot.data(), 32, out) == Step::Error);
  auto withLong = fatName({'a'});
  std::memcpy(withLong.back().data(), ".          ", 11);
  withLong.back()[11] = 0x10;
  withLong.front()[13] = fatSum(withLong.back());
  DirectoryEnumerator dotWithLong(Format::Fat);
  assert(dotWithLong.accept(withLong.front().data(), 32, out) == Step::More);
  assert(dotWithLong.accept(withLong.back().data(), 32, out) == Step::Error);
  Rec internalDot = shortEntry;
  internalDot[2] = '.';
  DirectoryEnumerator badShort(Format::Fat);
  assert(badShort.accept(internalDot.data(), 32, out) == Step::Error);
}
