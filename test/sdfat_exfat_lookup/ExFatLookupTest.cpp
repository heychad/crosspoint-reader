#include <ExFatLib/ExFatFile.h>
#include <ExFatLib/ExFatFormatter.h>
#include <ExFatLib/ExFatVolume.h>
#include <common/FsBlockDeviceInterface.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>

struct Disk : FsBlockDeviceInterface {
  std::unordered_map<Sector_t, std::array<uint8_t, 512>> sectors;
  int reads = 0;
  int failAt = -1;
  bool injected = false;
  bool isBusy() override { return false; }
  Sector_t sectorCount() override { return 1024 * 1024; }
  bool syncDevice() override { return true; }
  bool readSector(Sector_t sector, uint8_t* target) override {
    assert(sector < sectorCount());
    if (reads++ == failAt) { injected = true; return false; }
    const auto found = sectors.find(sector);
    if (found == sectors.end()) memset(target, 0, 512);
    else memcpy(target, found->second.data(), 512);
    return true;
  }
  bool readSectors(Sector_t sector, uint8_t* target, size_t count) override {
    for (size_t i = 0; i < count; ++i) if (!readSector(sector + i, target + 512 * i)) return false;
    return true;
  }
  bool writeSector(Sector_t sector, const uint8_t* source) override {
    assert(sector < sectorCount());
    memcpy(sectors[sector].data(), source, 512);
    return true;
  }
  bool writeSectors(Sector_t sector, const uint8_t* source, size_t count) override {
    for (size_t i = 0; i < count; ++i) if (!writeSector(sector + i, source + 512 * i)) return false;
    return true;
  }
};

int main() {
  Disk disk;
  uint8_t scratch[512];
  ExFatFormatter formatter;
  assert(formatter.format(&disk, scratch));
  ExFatVolume volume;
  assert(volume.begin(&disk));
  ExFatFile root;
  assert(root.openRoot(&volume));
  ExFatFile directory;
  assert(directory.mkdir(&root, "bookmarks"));
  // Force the checked scan across many sectors before the target entries.
  for (int i = 0; i < 64; ++i) {
    ExFatFile filler;
    const auto fillerName = "filler-" + std::to_string(i) + ".txt";
    assert(filler.open(&directory, fillerName.c_str(), O_CREAT | O_WRONLY));
    assert(filler.close());
  }
  const std::string first = "Alpha - A long reading list Café 世界.epub";
  const std::string second = "Bravo - A long reading list Café 世界.epub";
  ExFatFile file;
  assert(file.open(&directory, first.c_str(), O_CREAT | O_WRONLY));
  const uint32_t firstIndex = file.dirIndex();
  assert(file.write("saved", 5) == 5);
  assert(file.close());
  assert(file.open(&directory, second.c_str(), O_CREAT | O_WRONLY));
  assert(file.close());
  directory.setStrictLookup(true);
  assert(file.open(&directory, second.c_str(), O_RDONLY));
  assert(file.close());
  assert(!file.open(&directory, "not present.epub", O_RDONLY));
  assert(directory.getError() == 0);
  assert(directory.close());
  assert(root.close());
  assert(volume.cacheClear());

  int baselineReads = 0;
  int failures = 0;
  for (int fault = -1; fault < 128; ++fault) {
    disk.failAt = -1;
    disk.injected = false;
    ExFatFile parentRoot;
    assert(parentRoot.openRoot(&volume));
    ExFatFile parent;
    assert(parent.open(&parentRoot, "bookmarks", O_RDONLY));
    assert(parentRoot.close());
    parent.setStrictLookup(true);
    assert(volume.cacheClear());
    disk.reads = 0;
    disk.failAt = fault;
    ExFatFile checked;
    const bool found = checked.open(&parent, second.c_str(), O_RDONLY);
    const bool error = parent.getError() != 0;
    if (fault == -1) {
      assert(found);
      baselineReads = disk.reads;
    }
    if (disk.injected) {
      ++failures;
      if (found || !error) {
        fprintf(stderr, "exFAT hidden read failure %d found=%d error=%d\n", fault, found, error);
        return 2;
      }
    }
    disk.failAt = -1;
    if (checked) assert(checked.close());
    assert(parent.close());
    assert(volume.cacheClear());
  }
  if (baselineReads <= 4 || failures != baselineReads) {
    fprintf(stderr, "exFAT insufficient fault coverage: baseline=%d failures=%d\n", baselineReads, failures);
    return 2;
  }

  struct Damage { uint32_t index; unsigned byte; uint8_t mask; };
  for (const auto& damaged : {Damage{firstIndex, 2, 1}, Damage{firstIndex + 1, 0, 1},
                               Damage{firstIndex + 2, 2, 1}, Damage{firstIndex, 1, 1},
                               Damage{firstIndex, 0, 0x20}, Damage{firstIndex, 1, 0x20}}) {
    ExFatFile parentRoot;
    assert(parentRoot.openRoot(&volume));
    ExFatFile parent;
    assert(parent.open(&parentRoot, "bookmarks", O_RDONLY));
    assert(parentRoot.close());
    const uint32_t byteOffset = damaged.index * 32;
    assert(parent.seekSet(byteOffset + 1));
    const auto clusterBytes = volume.bytesPerCluster();
    const auto sector = volume.clusterHeapStartSector() + (parent.curCluster() - 2) * volume.sectorsPerCluster() +
                        (byteOffset % clusterBytes) / 512;
    const size_t inSector = byteOffset % 512 + damaged.byte;
    assert(parent.close());
    assert(volume.cacheClear());
    disk.sectors[sector][inSector] ^= damaged.mask;

    assert(parentRoot.openRoot(&volume));
    assert(parent.open(&parentRoot, "bookmarks", O_RDONLY));
    assert(parentRoot.close());
    ExFatFile intact;
    const bool ordinary = intact.open(&parent, second.c_str(), O_RDONLY);
    if (intact) assert(intact.close());
    assert(parent.close());
    assert(volume.cacheClear());

    assert(parentRoot.openRoot(&volume));
    assert(parent.open(&parentRoot, "bookmarks", O_RDONLY));
    assert(parentRoot.close());
    parent.setStrictLookup(true);
    const bool strict = intact.open(&parent, second.c_str(), O_RDONLY);
    const bool error = parent.getError() != 0;
    if (intact) assert(intact.close());
    assert(parent.close());
    assert(volume.cacheClear());
    disk.sectors[sector][inSector] ^= damaged.mask;
    if (!ordinary || strict || !error) {
      fprintf(stderr, "exFAT scope mismatch ordinary=%d strict=%d error=%d\n", ordinary, strict, error);
      return 4;
    }
  }

  // A checksum-valid benign extension is allowed; the same unknown critical
  // secondary must fail even when its entry-set checksum is correct.
  ExFatFile extensionRoot, extensionDir, extensionFile;
  assert(extensionRoot.openRoot(&volume));
  assert(extensionDir.open(&extensionRoot, "bookmarks", O_RDONLY));
  assert(extensionRoot.close());
  assert(extensionFile.open(&extensionDir, "Note.epub", O_CREAT | O_WRONLY));
  const uint32_t extensionIndex = extensionFile.dirIndex();
  assert(extensionFile.close());
  std::array<std::pair<Sector_t, size_t>, 4> slots;
  const uint32_t clusterBytes = volume.bytesPerCluster();
  for (size_t i = 0; i < slots.size(); ++i) {
    const uint32_t position = (extensionIndex + i) * 32;
    assert(extensionDir.seekSet(position + 1));
    const auto sector = volume.clusterHeapStartSector() + (extensionDir.curCluster() - 2) * volume.sectorsPerCluster() +
                        (position % clusterBytes) / 512;
    slots[i] = {sector, position % 512};
  }
  assert(extensionDir.close());
  assert(volume.cacheClear());
  const auto slot = [&](size_t index) { return disk.sectors[slots[index].first].data() + slots[index].second; };
  if (slot(0)[1] != 2 || slot(3)[0] != 0) {
    fprintf(stderr, "unexpected exFAT extension fixture layout\n");
    return 5;
  }
  slot(0)[1] = 3;
  memset(slot(3), 0, 32);
  const auto setExtension = [&](uint8_t type) {
    slot(3)[0] = type;
    uint16_t checksum = 0;
    for (size_t i = 0; i < slots.size(); ++i) {
      for (unsigned j = 0; j < 32; ++j) {
        if (i == 0 && (j == 2 || j == 3)) continue;
        checksum = static_cast<uint16_t>(checksum / 2 + (checksum % 2) * 32768 + slot(i)[j]);
      }
    }
    slot(0)[2] = checksum & 0xff;
    slot(0)[3] = checksum >> 8;
  };
  const auto checkedOpen = [&](bool expectSuccess) {
    ExFatFile parentRoot, parent, selected;
    assert(parentRoot.openRoot(&volume));
    assert(parent.open(&parentRoot, "bookmarks", O_RDONLY));
    assert(parentRoot.close());
    parent.setStrictLookup(true);
    const bool found = selected.open(&parent, second.c_str(), O_RDONLY);
    const bool error = parent.getError() != 0;
    if (selected) assert(selected.close());
    assert(parent.close());
    assert(volume.cacheClear());
    return found == expectSuccess && error != expectSuccess;
  };
  setExtension(0xe2);
  if (!checkedOpen(true)) {
    fprintf(stderr, "checksum-valid benign exFAT extension was rejected\n");
    return 6;
  }
  setExtension(0xc2);
  if (!checkedOpen(false)) {
    fprintf(stderr, "unknown critical exFAT extension was accepted\n");
    return 6;
  }
  printf("exFAT valid, missing, six damaged entry sets, and ordinary lookup isolation passed\n");
  printf("exFAT baseline reads: %d; injected read failures rejected: %d\n", baselineReads, failures);
  printf("exFAT benign and critical secondary policies passed\n");
}
