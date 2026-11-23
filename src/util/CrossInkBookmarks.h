#pragma once

#include <HalStorage.h>

#include <array>
#include <cstdint>
#include <string>

#include "CrossInkBookmarkSnapshot.h"

// The CrossInk file stays authoritative. Opening and jumping never rewrites it.
class CrossInkBookmarks {
 public:
  struct Entry {
    uint16_t spine = 0;
    float progress = 0;
    uint16_t paragraph = UINT16_MAX;
    bool recovered = false;
    char chapter[48]{};
    char snippet[64]{};
  };
  bool open(const std::string& bookPath);
  // Caller holds the storage lock. Closes verified sources before removal.
  // Each file is removed separately; false can leave an earlier source cleared.
  static bool clearForBook(const std::string& bookPath);
  bool close();
  uint16_t count() const { return entryCount; }
  bool unreadable() const { return invalidFile; }
  bool read(uint16_t index, Entry& entry);
  bool canRemove(uint16_t index) const;
  bool selectForRemoval(uint16_t index, Entry& entry, crossinkbookmarks::RemovalTarget& target);

 private:
  struct Source {
    HalFile file;
    size_t key = 0;
    uint32_t recordsOffset = 0;
    uint16_t count = 0;
    uint16_t recordSize = 0;
    uint8_t version = 0;
    bool recovered = false;
  };
  bool readHeader(Source& source, const std::string& bookPath);
  std::array<Source, 2> sources;
  uint16_t entryCount = 0;
  bool invalidFile = false;
};
