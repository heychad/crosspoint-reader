#include <FatLib/FatFile.h>
#include <FatLib/FatFormatter.h>
#include <FatLib/FatVolume.h>
#include <common/FsBlockDeviceInterface.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "CheckedStoragePath.h"

struct Disk : FsBlockDeviceInterface {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(64 * 1024 * 1024);
  std::vector<Sector_t> reads;
  int failAt = -1;
  bool injected = false;
  bool isBusy() override { return false; }
  Sector_t sectorCount() override { return bytes.size() / 512; }
  bool syncDevice() override { return true; }
  bool readSector(Sector_t sector, uint8_t* target) override {
    const int index = reads.size();
    reads.push_back(sector);
    if (index == failAt) {
      injected = true;
      return false;
    }
    assert(sector < sectorCount());
    memcpy(target, bytes.data() + sector * 512, 512);
    return true;
  }
  bool readSectors(Sector_t sector, uint8_t* target, size_t count) override {
    for (size_t i = 0; i < count; ++i)
      if (!readSector(sector + i, target + 512 * i)) return false;
    return true;
  }
  bool writeSector(Sector_t sector, const uint8_t* source) override {
    assert(sector < sectorCount());
    memcpy(bytes.data() + sector * 512, source, 512);
    return true;
  }
  bool writeSectors(Sector_t sector, const uint8_t* source, size_t count) override {
    for (size_t i = 0; i < count; ++i)
      if (!writeSector(sector + i, source + 512 * i)) return false;
    return true;
  }
};
int main() {
  Disk disk;
  uint8_t scratch[512];
  FatFormatter formatter;
  assert(formatter.format(&disk, scratch));
  FatVolume volume;
  assert(volume.begin(&disk));
  FatFile root;
  assert(root.openRoot(&volume));
  for (const auto& invalid : {std::string("bad:name"), std::string(256, 'a'), std::string("bad\xc3")}) {
    assert(!validCheckedStorageComponent(invalid.c_str()));
    FatFile rejected;
    assert(!rejected.open(&root, invalid.c_str(), O_RDONLY));
  }
  assert(validCheckedStorageComponent("Café 世界.epub"));
  assert(validCheckedStorageComponent(std::string(255, 'a').c_str()));
  FatFile directory;
  assert(directory.mkdir(&root, "bookmarks"));
  const size_t clusterBytes = volume.bytesPerCluster();
  const int entriesPerCluster = clusterBytes / 32;
  int fillerIndex = 0;
  const auto fillTo = [&](int count) {
    while (fillerIndex < count) {
      FatFile filler;
      const auto shortName = "F" + std::to_string(fillerIndex++) + ".TXT";
      assert(filler.open(&directory, shortName.c_str(), O_CREAT | O_WRONLY));
      assert(filler.close());
    }
  };
  fillTo(entriesPerCluster - 2);
  FatFile gapOne;
  assert(gapOne.open(&root, "GAP1.BIN", O_CREAT | O_WRONLY));
  assert(gapOne.preAllocate(clusterBytes * 512));
  assert(gapOne.close());
  fillTo(entriesPerCluster * 2 - 12);
  FatFile gapTwo;
  assert(gapTwo.open(&root, "GAP2.BIN", O_CREAT | O_WRONLY));
  assert(gapTwo.preAllocate(clusterBytes * 512));
  assert(gapTwo.close());
  const std::string name = "Books_" + std::string(180, 'L') + " Café 世界.json";
  FatFile file;
  assert(file.open(&directory, name.c_str(), O_CREAT | O_WRONLY));
  assert(file.dirIndex() >= entriesPerCluster * 2);
  const uint32_t bookmarkIndex = file.dirIndex();
  assert(file.write("saved", 5) == 5);
  assert(file.close());
  const std::string otherName = "Other_" + std::string(180, 'A') + " Café 世界.json";
  FatFile other;
  assert(other.open(&directory, otherName.c_str(), O_CREAT | O_WRONLY));
  assert(other.close());
  directory.setStrictLookup(true);
  FatFile checked;
  assert(checked.open(&directory, otherName.c_str(), O_RDONLY));
  assert(checked.close());
  assert(!checked.open(&directory, "not present.epub", O_RDONLY));
  assert(directory.getError() == 0);
  directory.setStrictLookup(false);
  assert(directory.close());
  assert(root.close());
  assert(volume.cacheClear());
  int baselineReads = 0;
  int faultCases = 0;
  int fatFaults = 0;
  for (int fault = -1; fault < 128; ++fault) {
    disk.failAt = -1;
    disk.injected = false;
    disk.reads.clear();
    FatFile parentRoot;
    assert(parentRoot.openRoot(&volume));
    FatFile parent;
    assert(parent.open(&parentRoot, "bookmarks", O_RDONLY));
    assert(parentRoot.close());
    const auto firstCluster = parent.firstCluster();
    assert(parent.seekSet(clusterBytes + 1));
    const auto secondCluster = parent.curCluster();
    assert(parent.seekSet(clusterBytes * 2 + 1));
    const auto thirdCluster = parent.curCluster();
    assert(secondCluster > firstCluster + 256);
    assert(thirdCluster > secondCluster + 256);
    assert(parent.seekSet(0));
    assert(volume.cacheClear());
    disk.reads.clear();
    disk.failAt = fault;
    FatFile bookmark;
    const bool found = bookmark.open(&parent, name.c_str(), O_RDONLY);
    const bool error = parent.getError() != 0;
    if (fault == -1) {
      assert(found);
      baselineReads = disk.reads.size();
    }
    if (disk.injected) {
      ++faultCases;
      const auto sector = disk.reads.at(fault);
      if (sector >= volume.fatStartSector() && sector < volume.fatStartSector() + volume.sectorsPerFat()) ++fatFaults;
      if (!error) {
        fprintf(stderr, "hidden read failure %d, found=%d, reads=%zu\n", fault, found, disk.reads.size());
        return 2;
      }
    }
    disk.failAt = -1;
    if (bookmark) assert(bookmark.close());
    assert(parent.close());
  }
  assert(faultCases >= 3);
  assert(fatFaults > 0);
  int exclusiveFaults = 0;
  for (int fault = -1; fault < 128; ++fault) {
    disk.failAt = -1;
    disk.injected = false;
    disk.reads.clear();
    FatFile parentRoot;
    assert(parentRoot.openRoot(&volume));
    FatFile parent;
    assert(parent.open(&parentRoot, "bookmarks", O_RDONLY));
    assert(parentRoot.close());
    assert(volume.cacheClear());
    disk.reads.clear();
    disk.failAt = fault;
    FatFile duplicate;
    const bool created = duplicate.open(&parent, name.c_str(), O_CREAT | O_EXCL | O_WRONLY);
    if (disk.injected) ++exclusiveFaults;
    if (created) {
      fprintf(stderr, "exclusive create accepted existing LFN after fault %d\n", fault);
      return 3;
    }
    disk.failAt = -1;
    assert(parent.close());
  }
  assert(exclusiveFaults >= 3);
  // A matching long name with a damaged short-entry checksum or LFN chain
  // must be treated as an error, never as an absent book.
  struct Damage {
    uint32_t index;
    unsigned byte;
    uint8_t mask;
  };
  constexpr uint32_t firstLongEntry = (199 + 12) / 13;
  for (const auto& damaged :
       {Damage{bookmarkIndex, 0, 1}, Damage{bookmarkIndex - 1, 13, 1}, Damage{bookmarkIndex - firstLongEntry, 0, 0x40},
        Damage{bookmarkIndex - firstLongEntry, 0, 1}}) {
    FatFile parentRoot;
    assert(parentRoot.openRoot(&volume));
    FatFile parent;
    assert(parent.open(&parentRoot, "bookmarks", O_RDONLY));
    assert(parentRoot.close());
    const uint32_t byteOffset = damaged.index * 32;
    assert(parent.seekSet(byteOffset + 1));
    const auto sector = volume.dataStartSector() + (parent.curCluster() - 2) * volume.sectorsPerCluster() +
                        (byteOffset % clusterBytes) / 512;
    const size_t absolute = static_cast<size_t>(sector) * 512 + byteOffset % 512 + damaged.byte;
    assert(parent.close());
    assert(volume.cacheClear());
    disk.bytes[absolute] ^= damaged.mask;
    assert(parentRoot.openRoot(&volume));
    assert(parent.open(&parentRoot, "bookmarks", O_RDONLY));
    assert(parentRoot.close());
    parent.setStrictLookup(true);
    FatFile bad;
    const bool found = bad.open(&parent, name.c_str(), O_RDONLY);
    const bool error = parent.getError() != 0;
    if (bad) assert(bad.close());
    assert(parent.close());
    assert(volume.cacheClear());
    // A standard open still reaches an intact, later file. The checked mode
    // refuses to treat the same damaged directory as safely searchable.
    assert(parentRoot.openRoot(&volume));
    assert(parent.open(&parentRoot, "bookmarks", O_RDONLY));
    assert(parentRoot.close());
    FatFile intact;
    const bool ordinaryFound = intact.open(&parent, otherName.c_str(), O_RDONLY);
    if (intact) assert(intact.close());
    assert(parent.close());
    assert(volume.cacheClear());
    assert(parentRoot.openRoot(&volume));
    assert(parent.open(&parentRoot, "bookmarks", O_RDONLY));
    assert(parentRoot.close());
    parent.setStrictLookup(true);
    const bool strictFound = intact.open(&parent, otherName.c_str(), O_RDONLY);
    const bool strictError = parent.getError() != 0;
    if (intact) assert(intact.close());
    assert(parent.close());
    assert(volume.cacheClear());
    disk.bytes[absolute] ^= damaged.mask;
    if (found || !error) {
      fprintf(stderr, "corrupt LFN classified as %s without a directory error\n", found ? "present" : "missing");
      return 4;
    }
    if (!ordinaryFound || strictFound || !strictError) {
      fprintf(stderr, "scoped lookup mismatch ordinary=%d strict=%d error=%d\n", ordinaryFound, strictFound,
              strictError);
      return 5;
    }
  }
  printf("FAT-chain read faults exercised: %d\n", fatFaults);
  printf("fragmented FAT LFN: %d baseline reads, %d one-shot faults observed\n", baselineReads, faultCases);
  printf("existing exclusive LFN: %d one-shot faults rejected\n", exclusiveFaults);
  printf("corrupt matching long-name records rejected\n");
}
