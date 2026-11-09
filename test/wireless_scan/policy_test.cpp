#include <array>
#include <cassert>

#include "DirectoryValidator.h"
using namespace wirecheck;
int main() {
  std::array<uint8_t, 32> entry{};
  DirectoryValidator incompleteFat(Format::Fat);
  entry[0] = 0x42;
  entry[11] = 0x0f;
  entry[13] = 5;
  assert(incompleteFat.accept(entry.data(), 32) == Result::More);
  assert(incompleteFat.finish() == Result::Error);
  DirectoryValidator brokenFat(Format::Fat);
  assert(brokenFat.accept(entry.data(), 32) == Result::More);
  entry[0] = 0x41;
  assert(brokenFat.accept(entry.data(), 32) == Result::Error);
  DirectoryValidator limit(Format::Fat);
  entry = {};
  entry[0] = 0xe5;
  for (int i = 0; i < 4096; ++i) assert(limit.accept(entry.data(), 32) == Result::More);
  assert(limit.accept(entry.data(), 32) == Result::Error);
  DirectoryValidator incompleteExfat(Format::ExFat);
  entry = {};
  entry[0] = 0x85;
  entry[1] = 2;
  assert(incompleteExfat.accept(entry.data(), 32) == Result::More);
  assert(incompleteExfat.finish() == Result::Error);
  DirectoryValidator badStream(Format::ExFat);
  assert(badStream.accept(entry.data(), 32) == Result::More);
  entry = {};
  entry[0] = 0xc1;
  assert(badStream.accept(entry.data(), 32) == Result::Error);
  DirectoryValidator invalid(Format::Fat);
  assert(invalid.accept(nullptr, 32) == Result::Error);
  DirectoryValidator terminal(Format::ExFat);
  entry = {};
  assert(terminal.accept(entry.data(), 32) == Result::End);
  assert(terminal.accept(entry.data(), 32) == Result::Error);
}
