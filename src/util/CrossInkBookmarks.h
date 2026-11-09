#pragma once

#include <HalStorage.h>

#include <array>
#include <cstdint>
#include <string>

// The CrossInk file stays authoritative. Opening and jumping never rewrites it.
class CrossInkBookmarks {
 public:
  struct Entry {
    uint16_t spine = 0;
    float progress = 0;
    uint16_t paragraph = UINT16_MAX;
    char chapter[48]{};
    char snippet[64]{};
  };
  bool open(const std::string& bookPath);
  void close();
  uint16_t count() const { return entryCount; }
  bool unreadable() const { return invalidFile; }
  bool read(uint16_t index, Entry& entry);

 private:
  struct Source {
    HalFile file;
    uint32_t recordsOffset = 0;
    uint16_t count = 0;
    uint16_t recordSize = 0;
    uint8_t version = 0;
  };
  bool readHeader(Source& source, const std::string& bookPath);
  std::array<Source, 2> sources;
  uint16_t entryCount = 0;
  bool invalidFile = false;
};
