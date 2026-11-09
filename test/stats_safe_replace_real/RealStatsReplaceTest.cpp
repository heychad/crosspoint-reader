#ifdef NDEBUG
#error "This test requires assertions"
#endif
#include <ExFatLib/ExFatFormatter.h>
#include <FatLib/FatFormatter.h>
#include <FsLib/FsVolume.h>
#include <HalStorage.h>
#include <StatsSafeReplace.h>
#include <common/FsBlockDeviceInterface.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

struct Disk : FsBlockDeviceInterface {
  std::unordered_map<Sector_t, std::array<uint8_t, 512>> sectors;
  Sector_t sectorsTotal = 1024 * 1024;
  Sector_t failReadSector = static_cast<Sector_t>(-1);
  bool injectedRead = false;
  int reads = 0;
  int writes = 0, failAt = -1;
  bool injected = false, dead = false, failStop = false;
  bool isBusy() override { return false; }
  Sector_t sectorCount() override { return sectorsTotal; }
  bool syncDevice() override { return !dead; }
  bool readSector(Sector_t n, uint8_t* out) override {
    ++reads;
    if (n == failReadSector) {
      failReadSector = static_cast<Sector_t>(-1);
      injectedRead = true;
      return false;
    }
    if (dead) return false;
    assert(n < sectorCount());
    const auto it = sectors.find(n);
    if (it == sectors.end())
      std::memset(out, 0, 512);
    else
      std::memcpy(out, it->second.data(), 512);
    return true;
  }
  bool readSectors(Sector_t n, uint8_t* out, size_t count) override {
    for (size_t i = 0; i < count; ++i)
      if (!readSector(n + i, out + 512 * i)) return false;
    return true;
  }
  bool writeSector(Sector_t n, const uint8_t* in) override {
    if (dead) return false;
    if (writes++ == failAt) {
      injected = true;
      dead = failStop;
      return false;
    }
    assert(n < sectorCount());
    std::memcpy(sectors[n].data(), in, 512);
    return true;
  }
  bool writeSectors(Sector_t n, const uint8_t* in, size_t count) override {
    for (size_t i = 0; i < count; ++i)
      if (!writeSector(n + i, in + 512 * i)) return false;
    return true;
  }
};
#ifndef STATS_PAYLOAD_SIZE
#define STATS_PAYLOAD_SIZE 73
#endif
using Bytes = std::array<uint8_t, STATS_PAYLOAD_SIZE>;
bool writeFile(const char* path, const Bytes& bytes) {
  auto f = Storage.open(path, O_CREAT | O_EXCL | O_WRONLY);
  if (!f) return false;
  const bool wrote = f.write(bytes.data(), bytes.size()) == bytes.size();
  return f.close() && wrote;
}
bool readFile(const char* path, Bytes& bytes) {
  auto f = Storage.open(path, O_RDONLY);
  if (!f) return false;
  const bool read =
      f.fileSize() == bytes.size() && f.read(bytes.data(), bytes.size()) == static_cast<int>(bytes.size());
  return f.close() && read;
}
void exercise(bool exfat, bool initial, bool nested) {
  const std::string primary = nested ? "/crosspoint/reading_stats/epub_123/stats_v5.bin" : "/stats.bin";
  const std::string temp = primary + ".tmp", backup = primary + ".bak";
  Disk baseline;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter f;
    assert(f.format(&baseline, scratch));
  } else {
    FatFormatter f;
    assert(f.format(&baseline, scratch));
  }
  Bytes old{}, update{}, later{};
  old.fill(17);
  update.fill(34);
  later.fill(51);
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    if (nested) assert(volume.mkdir("/crosspoint/reading_stats/epub_123", true));
    if (!initial) assert(writeFile(primary.c_str(), old));
  }
  for (bool stop : {false, true}) {
    int injected = 0;
    for (int fault = -1; fault < 64; ++fault) {
      Disk disk;
      disk.sectors = baseline.sectors;
      bool saved = false;
      {
        FsVolume volume;
        assert(volume.begin(&disk));
        Storage.attach(volume);
        disk.writes = 0;
        disk.failAt = fault;
        disk.failStop = stop;
        saved = statsreplace::save("TEST", primary, update.data(), update.size(), true);
      }
      if (fault >= 0 && !disk.injected) break;
      if (disk.injected) ++injected;
      disk.dead = false;
      disk.failAt = -1;
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      const char* paths[] = {primary.c_str(), temp.c_str(), backup.c_str()};
      std::array<bool, 3> hadOld{}, hadNew{};
      for (size_t i = 0; i < 3; ++i) {
        Bytes bytes{};
        if (readFile(paths[i], bytes)) {
          hadOld[i] = bytes == old;
          hadNew[i] = bytes == update;
        }
      }
      if (!initial) assert(hadOld[0] || hadOld[1] || hadOld[2] || hadNew[0] || hadNew[1] || hadNew[2]);
      if (saved) assert(hadNew[0] && !Storage.exists(temp.c_str()));
      assert(writeFile("/later.bin", later));
      FsVolume after;
      assert(after.begin(&disk));
      Storage.attach(after);
      for (size_t i = 0; i < 3; ++i)
        if (hadOld[i] || hadNew[i]) {
          Bytes bytes{};
          assert(readFile(paths[i], bytes));
          assert(bytes == (hadOld[i] ? old : update));
        }
    }
    std::printf("%s initial=%d nested=%d %s injected=%d preserved after later allocation\n", exfat ? "exFAT" : "FAT",
                initial, nested, stop ? "fail-stop" : "transient", injected);
    assert(injected > 0);
  }
}
void exerciseCrosslinkedFatTail(bool backupFirst, bool fat32) {
  Disk disk;
  if (fat32) disk.sectorsTotal = 64 * 1024 * 1024;
  uint8_t scratch[512];
  FatFormatter formatter;
  assert(formatter.format(&disk, scratch));
  Cluster_t primaryCluster = 0, backupCluster = 0;
  Sector_t fatStart = 0;
  uint8_t fatType = 0, fatCount = 0;
  Bytes original{}, backup{}, update{}, later{};
  original.fill(17);
  backup.fill(34);
  update.fill(51);
  later.fill(68);
  {
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    fatStart = volume.fatStartSector();
    fatType = volume.fatType();
    fatCount = volume.fatCount();
    if (backupFirst)
      assert(writeFile("/stats.bin.bak", backup) && writeFile("/stats.bin", original));
    else
      assert(writeFile("/stats.bin", original) && writeFile("/stats.bin.bak", backup));
    auto primary = volume.open("/stats.bin", O_RDONLY), saved = volume.open("/stats.bin.bak", O_RDONLY);
    assert(primary && saved);
    primaryCluster = primary.firstCluster();
    backupCluster = saved.firstCluster();
    assert(primary.close() && saved.close());
  }
  assert(primaryCluster && backupCluster && primaryCluster != backupCluster);
  assert(fatType == (fat32 ? 32 : 16));
  assert(backupFirst ? backupCluster + 1 == primaryCluster : primaryCluster + 1 == backupCluster);
  const auto le16 = [](const uint8_t* p) { return uint16_t(p[0] | uint16_t(p[1]) << 8); };
  const auto le32 = [&](const uint8_t* p) { return uint32_t(le16(p) | uint32_t(le16(p + 2)) << 16); };
  uint8_t mbr[512], boot[512];
  assert(disk.readSector(0, mbr) && disk.readSector(le32(mbr + 454), boot));
  const uint32_t sectorsPerFat = le16(boot + 22) ? le16(boot + 22) : le32(boot + 36);
  assert((fatType == 16 || fatType == 32) && sectorsPerFat);
  const unsigned width = fatType == 16 ? 2 : 4;
  const uint64_t offset = uint64_t(backupCluster) * width;
  for (unsigned i = 0; i < fatCount; ++i) {
    auto& sector = disk.sectors[fatStart + Sector_t(i) * sectorsPerFat + offset / 512];
    for (unsigned j = 0; j < width; ++j) sector[offset % 512 + j] = uint8_t(primaryCluster >> (8 * j));
  }
  FsVolume remount;
  assert(remount.begin(&disk));
  Storage.attach(remount);
  assert(!Storage.distinctFirstClusters("/stats.bin", "/stats.bin.bak"));
  assert(!statsreplace::save("TEST", "/stats.bin", update.data(), update.size(), false));
  assert(Storage.exists("/stats.bin.bak"));
  assert(writeFile("/later.bin", later));
  FsVolume after;
  assert(after.begin(&disk));
  Storage.attach(after);
  Bytes read{};
  assert(readFile("/stats.bin", read) && read == original);
  std::printf("FAT crosslinked tail (backup first=%d) refused without freeing the primary\n", backupFirst);
}

void exerciseFatChainedExfat() {
  Disk disk;
  uint8_t scratch[512];
  ExFatFormatter formatter;
  assert(formatter.format(&disk, scratch));
  {
    FsVolume volume;
    assert(volume.begin(&disk));
    auto file = volume.open("/chained.bin", O_CREAT | O_EXCL | O_RDWR);
    assert(file);
    uint8_t bytes[73]{};
    assert(file.write(bytes, sizeof(bytes)) == sizeof(bytes) && file.sync());
    auto blocker = volume.open("/blocker.bin", O_CREAT | O_EXCL | O_WRONLY);
    assert(blocker && blocker.write(uint8_t{1}) == 1 && blocker.close());
    const std::string extension(volume.bytesPerCluster(), 'x');
    assert(file.write(extension.data(), extension.size()) == extension.size());
    assert(!file.isContiguous() && file.truncate(sizeof(bytes)) && file.close());
  }
  FsVolume after;
  assert(after.begin(&disk));
  auto file = after.open("/chained.bin", O_RDONLY);
  assert(file && !file.isContiguous() && file.smallSingleClusterChain() && file.settingsSingleClusterChain() &&
         file.close());
  std::puts("exFAT one-cluster FAT-chain accepted");
}

void exerciseLongThirdFileTail() {
  Disk disk;
  disk.sectorsTotal = 64 * 1024 * 1024;
  uint8_t scratch[512];
  FatFormatter formatter;
  assert(formatter.format(&disk, scratch));
  Cluster_t backupCluster = 0, thirdCluster = 0;
  Sector_t fatStart = 0;
  uint8_t fatCount = 0;
  Bytes original{}, other{}, update{}, later{};
  original.fill(17);
  other.fill(34);
  update.fill(51);
  later.fill(68);
  {
    FsVolume volume;
    assert(volume.begin(&disk) && volume.fatType() == 32);
    Storage.attach(volume);
    fatStart = volume.fatStartSector();
    fatCount = volume.fatCount();
    assert(writeFile("/stats.bin", original) && writeFile("/stats.bin.bak", original) &&
           writeFile("/other.bin", other));
    auto backup = volume.open("/stats.bin.bak", O_RDONLY), third = volume.open("/other.bin", O_RDONLY);
    assert(backup && third);
    backupCluster = backup.firstCluster();
    thirdCluster = third.firstCluster();
    assert(backupCluster + 1 == thirdCluster && backup.close() && third.close());
  }
  const auto le16 = [](const uint8_t* p) { return uint16_t(p[0] | uint16_t(p[1]) << 8); };
  const auto le32 = [&](const uint8_t* p) { return uint32_t(le16(p) | uint32_t(le16(p + 2)) << 16); };
  uint8_t mbr[512], boot[512];
  assert(disk.readSector(0, mbr) && disk.readSector(le32(mbr + 454), boot));
  const uint32_t sectorsPerFat = le32(boot + 36);
  assert(sectorsPerFat);
  for (uint32_t cluster = backupCluster; cluster < backupCluster + 8192; ++cluster) {
    const uint64_t offset = uint64_t(cluster) * 4;
    for (unsigned i = 0; i < fatCount; ++i) {
      auto& sector = disk.sectors[fatStart + Sector_t(i) * sectorsPerFat + offset / 512];
      const uint32_t next = cluster + 1;
      for (unsigned j = 0; j < 4; ++j) sector[offset % 512 + j] = uint8_t(next >> (8 * j));
    }
  }
  FsVolume remount;
  assert(remount.begin(&disk));
  Storage.attach(remount);
  disk.reads = 0;
  assert(!Storage.distinctFirstClusters("/stats.bin", "/stats.bin.bak"));
  const int identityReads = disk.reads;
  assert(identityReads < 32);
  assert(!statsreplace::save("TEST", "/stats.bin", update.data(), update.size(), false));
  assert(writeFile("/later.bin", later));
  FsVolume after;
  assert(after.begin(&disk));
  Storage.attach(after);
  Bytes read{};
  assert(readFile("/other.bin", read) && read == other);
  std::printf("FAT32 long third-file tail refused; identity reads=%d\n", identityReads);
}

void exerciseFatChainReadFailure() {
  Disk disk;
  disk.sectorsTotal = 64 * 1024 * 1024;
  uint8_t scratch[512];
  FatFormatter formatter;
  assert(formatter.format(&disk, scratch));
  Cluster_t cluster = 0;
  Sector_t fatStart = 0;
  {
    FsVolume volume;
    assert(volume.begin(&disk) && volume.fatType() == 32);
    fatStart = volume.fatStartSector();
    auto filler = volume.open("/filler.bin", O_CREAT | O_EXCL | O_WRONLY);
    assert(filler);
    const std::string bytes(volume.bytesPerCluster() * 130, 'x');
    assert(filler.write(bytes.data(), bytes.size()) == bytes.size() && filler.close());
    auto file = volume.open("/stats.bin", O_CREAT | O_EXCL | O_WRONLY);
    assert(file && file.write(uint8_t{1}) == 1);
    cluster = file.firstCluster();
    assert(cluster >= 128 && file.close());
  }
  FsVolume remount;
  assert(remount.begin(&disk));
  auto file = remount.open("/stats.bin", O_RDONLY);
  assert(file);
  disk.failReadSector = fatStart + cluster / 128;
  assert(!file.smallSingleClusterChain() && disk.injectedRead);
  assert(file.smallSingleClusterChain());
  assert(file.close());
  std::puts("FAT32 chain read failure refused, then retry succeeded");
}

void exerciseSettingsClusters(bool fat32) {
  Disk disk;
  if (fat32) disk.sectorsTotal = 64 * 1024 * 1024;
  uint8_t scratch[512];
  FatFormatter formatter;
  assert(formatter.format(&disk, scratch));
  FsVolume volume;
  assert(volume.begin(&disk) && volume.fatType() == (fat32 ? 32 : 16));
  const size_t clusterBytes = volume.bytesPerCluster();
  assert(clusterBytes >= 512 && clusterBytes <= 32768);
  const size_t length = std::min<size_t>(2279, clusterBytes);
  const std::string payload(length, 's');
  for (const char* path : {"/settings.json", "/snapshot.json"}) {
    auto file = volume.open(path, O_CREAT | O_EXCL | O_WRONLY);
    assert(file && file.write(payload.data(), payload.size()) == payload.size() && file.close());
  }
  auto source = volume.open("/settings.json", O_RDONLY), snapshot = volume.open("/snapshot.json", O_RDONLY);
  assert(source && snapshot && settingsFilesHaveDifferentFirstClusters(source, snapshot));
  assert(source.close() && snapshot.close());
  auto large = volume.open("/large.json", O_CREAT | O_EXCL | O_WRONLY);
  const std::string excess(clusterBytes + 1, 'x');
  assert(large && large.write(excess.data(), excess.size()) == excess.size() && large.close());
  source = volume.open("/settings.json", O_RDONLY);
  large = volume.open("/large.json", O_RDONLY);
  assert(source && large && !settingsFilesHaveDifferentFirstClusters(source, large));
  assert(source.close() && large.close());
  std::printf("FAT%u settings single-cluster identity passed (cluster=%zu)\n", fat32 ? 32 : 16, clusterBytes);
}

void exerciseExfatSettingsClusters() {
  Disk disk;
  uint8_t scratch[512];
  ExFatFormatter formatter;
  assert(formatter.format(&disk, scratch));
  FsVolume volume;
  assert(volume.begin(&disk));
  const size_t clusterBytes = volume.bytesPerCluster();
  const size_t length = std::min<size_t>(2279, clusterBytes);
  const std::string payload(length, 's');
  for (const char* path : {"/settings.json", "/snapshot.json"}) {
    auto file = volume.open(path, O_CREAT | O_EXCL | O_WRONLY);
    assert(file && file.write(payload.data(), payload.size()) == payload.size() && file.close());
  }
  auto source = volume.open("/settings.json", O_RDONLY), snapshot = volume.open("/snapshot.json", O_RDONLY);
  assert(source && snapshot && settingsFilesHaveDifferentFirstClusters(source, snapshot));
  assert(source.close() && snapshot.close());
  auto large = volume.open("/large.json", O_CREAT | O_EXCL | O_WRONLY);
  const std::string excess(std::min<size_t>(32768, clusterBytes) + 1, 'x');
  assert(large && large.write(excess.data(), excess.size()) == excess.size() && large.close());
  source = volume.open("/settings.json", O_RDONLY);
  large = volume.open("/large.json", O_RDONLY);
  assert(source && large && !settingsFilesHaveDifferentFirstClusters(source, large));
  assert(source.close() && large.close());
  std::printf("exFAT settings single-cluster identity passed (cluster=%zu)\n", clusterBytes);
}

void exerciseFatTerminalMarkers(bool fat32) {
  Disk baseline;
  if (fat32) baseline.sectorsTotal = 64 * 1024 * 1024;
  uint8_t scratch[512];
  FatFormatter formatter;
  assert(formatter.format(&baseline, scratch));
  Cluster_t cluster = 0;
  Sector_t fatStart = 0;
  uint8_t fatCount = 0;
  {
    FsVolume volume;
    assert(volume.begin(&baseline) && volume.fatType() == (fat32 ? 32 : 16));
    fatStart = volume.fatStartSector();
    fatCount = volume.fatCount();
    auto file = volume.open("/stats.bin", O_CREAT | O_EXCL | O_WRONLY);
    assert(file && file.write(uint8_t{1}) == 1);
    cluster = file.firstCluster();
    assert(cluster >= 2 && file.close());
  }
  const auto le16 = [](const uint8_t* p) { return uint16_t(p[0] | uint16_t(p[1]) << 8); };
  const auto le32 = [&](const uint8_t* p) { return uint32_t(le16(p) | uint32_t(le16(p + 2)) << 16); };
  uint8_t mbr[512], boot[512];
  assert(baseline.readSector(0, mbr) && baseline.readSector(le32(mbr + 454), boot));
  const uint32_t sectorsPerFat = le16(boot + 22) ? le16(boot + 22) : le32(boot + 36);
  assert(sectorsPerFat);
  const uint32_t values[] = {fat32 ? 0x0FFFFFF7U : 0xFFF7U, fat32 ? 0x0FFFFFF0U : 0xFFF0U,
                             fat32 ? 0x0FFFFFF8U : 0xFFF8U, fat32 ? 0x0FFFFFFFU : 0xFFFFU};
  const unsigned width = fat32 ? 4 : 2;
  for (size_t index = 0; index < std::size(values); ++index) {
    Disk disk;
    disk.sectorsTotal = baseline.sectorsTotal;
    disk.sectors = baseline.sectors;
    const uint64_t offset = uint64_t(cluster) * width;
    for (unsigned i = 0; i < fatCount; ++i) {
      auto& sector = disk.sectors[fatStart + Sector_t(i) * sectorsPerFat + offset / 512];
      for (unsigned j = 0; j < width; ++j) sector[offset % 512 + j] = uint8_t(values[index] >> (8 * j));
    }
    FsVolume volume;
    assert(volume.begin(&disk));
    auto file = volume.open("/stats.bin", O_RDONLY);
    assert(file);
    const bool expected = index >= 2;
    assert(file.smallSingleClusterChain() == expected && file.settingsSingleClusterChain() == expected);
    assert(file.close());
  }
  std::printf("FAT%u bad and reserved markers refused; valid EOC accepted\n", fat32 ? 32 : 16);
}

void exerciseExfatAllocationBitmap() {
  Disk baseline;
  uint8_t scratch[512];
  ExFatFormatter formatter;
  assert(formatter.format(&baseline, scratch));
  Cluster_t cluster = 0;
  Sector_t bitmapStart = 0;
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    bitmapStart = volume.dataStartSector();
    auto file = volume.open("/settings.json", O_CREAT | O_EXCL | O_WRONLY);
    assert(file && file.write(uint8_t{42}) == 1);
    cluster = file.firstCluster();
    assert(cluster >= 2 && file.close());
  }
  const uint32_t bit = cluster - 2;
  const Sector_t sectorNumber = bitmapStart + bit / (512 * 8);
  const size_t byte = (bit / 8) % 512;
  const uint8_t mask = uint8_t{1} << (bit % 8);
  assert((baseline.sectors[sectorNumber][byte] & mask) != 0);
  {
    Disk disk;
    disk.sectors = baseline.sectors;
    disk.sectors[sectorNumber][byte] &= static_cast<uint8_t>(~mask);
    FsVolume volume;
    assert(volume.begin(&disk));
    auto file = volume.open("/settings.json", O_RDONLY);
    assert(file && !file.smallSingleClusterChain() && !file.settingsSingleClusterChain());
    uint8_t data = 0;
    assert(file.read(&data, 1) == 1 && data == 42 && file.close());
  }
  {
    Disk disk;
    disk.sectors = baseline.sectors;
    FsVolume volume;
    assert(volume.begin(&disk));
    auto file = volume.open("/settings.json", O_RDONLY);
    assert(file);
    disk.failReadSector = sectorNumber;
    assert(!file.settingsSingleClusterChain() && disk.injectedRead);
    assert(file.settingsSingleClusterChain() && file.close());
  }
  std::puts("exFAT free bitmap and bitmap read failure refused; retry succeeded");
}

int main() {
  for (bool exfat : {false, true})
    for (bool initial : {false, true})
      for (bool nested : {false, true}) exercise(exfat, initial, nested);
  exerciseCrosslinkedFatTail(false, false);
  exerciseCrosslinkedFatTail(true, false);
  exerciseCrosslinkedFatTail(false, true);
  exerciseCrosslinkedFatTail(true, true);
  exerciseFatChainedExfat();
  exerciseLongThirdFileTail();
  exerciseFatChainReadFailure();
  exerciseSettingsClusters(false);
  exerciseSettingsClusters(true);
  exerciseExfatSettingsClusters();
  exerciseFatTerminalMarkers(false);
  exerciseFatTerminalMarkers(true);
  exerciseExfatAllocationBitmap();
}
