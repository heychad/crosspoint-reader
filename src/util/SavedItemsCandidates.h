#pragma once

#include <array>
#include <bitset>
#include <memory>
#include <string_view>

#include "network/sync/StorageDirectoryScan.h"

namespace saveditems {
// Provisional absence checks for one catalog scan. The caller discards all
// filtered rows and starts over if the storage snapshot changes.
class Candidates {
 public:
  Candidates();
  bool advance();
  bool valid() const;
  bool empty() const;
  uint8_t kindsFor(std::string_view book) const;

 private:
  static constexpr unsigned STORE_COUNT = 2;
  static constexpr unsigned BIT_COUNT = 4096;
  struct Names {
    std::array<uint32_t, BIT_COUNT / 32> bits{};
    // A long-name match uses the same number of UTF-16 units even if its casing changes.
    std::bitset<256> lengths;
    std::bitset<256> nonAsciiLengths;
    bool reliable = true;
    bool any = false;
    void insert(std::string_view name);
    bool mightContain(std::string_view name) const;
  };
  static bool addEntry(void* context, const wirecheck::DirectoryEntry& entry);

  std::array<Names, STORE_COUNT> stores;
  HalStorage::MutationSnapshot snapshot;
  std::unique_ptr<wirecheck::StorageDirectoryScan> scan;
  unsigned current = 0;
  bool initialized = false;
};
}  // namespace saveditems
