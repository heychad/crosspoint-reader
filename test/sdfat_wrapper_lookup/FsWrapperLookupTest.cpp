#include <ExFatLib/ExFatFormatter.h>
#include <FatLib/FatFormatter.h>
#include <FsLib/FsFile.h>
#include <FsLib/FsVolume.h>
#include <common/FsBlockDeviceInterface.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

struct Disk : FsBlockDeviceInterface {
  std::unordered_map<Sector_t, std::array<uint8_t, 512>> sectors;
  bool isBusy() override { return false; }
  Sector_t sectorCount() override { return 1024 * 1024; }
  bool syncDevice() override { return true; }
  bool readSector(Sector_t sector, uint8_t* target) override {
    assert(sector < sectorCount());
    const auto found = sectors.find(sector);
    if (found == sectors.end())
      memset(target, 0, 512);
    else
      memcpy(target, found->second.data(), 512);
    return true;
  }
  bool readSectors(Sector_t sector, uint8_t* target, size_t count) override {
    for (size_t i = 0; i < count; ++i)
      if (!readSector(sector + i, target + 512 * i)) return false;
    return true;
  }
  bool writeSector(Sector_t sector, const uint8_t* source) override {
    assert(sector < sectorCount());
    memcpy(sectors[sector].data(), source, 512);
    return true;
  }
  bool writeSectors(Sector_t sector, const uint8_t* source, size_t count) override {
    for (size_t i = 0; i < count; ++i)
      if (!writeSector(sector + i, source + 512 * i)) return false;
    return true;
  }
};

int exercise(bool exfat) {
  Disk disk;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter formatter;
    if (!formatter.format(&disk, scratch)) return 1;
  } else {
    FatFormatter formatter;
    if (!formatter.format(&disk, scratch)) return 1;
  }
  FsVolume volume;
  if (!volume.begin(&disk, false)) return 2;
  FsBaseFile root, directory, file;
  if (!root.openRoot(&volume) || !directory.mkdir(&root, "books")) return 3;
  const std::string first = "Alpha - Long name Café 世界.epub";
  const std::string second = "Bravo - Long name Café 世界.epub";
  if (!file.open(&directory, first.c_str(), O_CREAT | O_WRONLY)) return 4;
  const uint32_t index = file.dirIndex();
  if (!file.close() || !file.open(&directory, second.c_str(), O_CREAT | O_WRONLY) || !file.close()) return 5;
  const uint32_t byteOffset = index * 32;
  if (!directory.seekSet(byteOffset + 1)) return 6;
  const auto sector = volume.dataStartSector() + (directory.curCluster() - 2) * volume.sectorsPerCluster() +
                      (byteOffset % volume.bytesPerCluster()) / 512;
  const size_t byte = byteOffset % 512 + (exfat ? 2 : 0);
  if (!directory.close() || !root.close()) return 7;
  disk.sectors[sector][byte] ^= 1;

  FsVolume reread;
  if (!reread.begin(&disk, false)) return 8;
  FsBaseFile ordinaryRoot, ordinaryDir, ordinaryFile;
  if (!ordinaryRoot.openRoot(&reread) || !ordinaryDir.open(&ordinaryRoot, "books", O_RDONLY) || !ordinaryRoot.close())
    return 9;
  const bool ordinary = ordinaryFile.open(&ordinaryDir, second.c_str(), O_RDONLY);
  if (ordinaryFile && !ordinaryFile.close()) return 10;
  if (!ordinaryDir.close()) return 11;

  FsBaseFile checkedRoot, checkedDir, checkedFile;
  if (!checkedRoot.openRoot(&reread) || !checkedRoot.enableStrictLookup() ||
      !checkedDir.open(&checkedRoot, "books", O_RDONLY) || !checkedRoot.close() || !checkedDir.enableStrictLookup())
    return 12;
  const bool found = checkedFile.open(&checkedDir, second.c_str(), O_RDONLY);
  const bool error = checkedDir.getError() != 0;
  if (checkedFile && !checkedFile.close()) return 13;
  if (!checkedDir.close()) return 14;
  if (!ordinary || found || !error) {
    fprintf(stderr, "%s wrapper mismatch ordinary=%d checked=%d error=%d\n", exfat ? "exFAT" : "FAT", ordinary, found,
            error);
    return 15;
  }
  printf("%s FsBaseFile scoped lookup passed\n", exfat ? "exFAT" : "FAT");
  return 0;
}

int main() {
  const int fat = exercise(false);
  if (fat) return fat;
  return exercise(true);
}
