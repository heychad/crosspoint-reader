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
  int writes = 0, failAt = -1;
  bool injected = false, dead = false, failStop = false;
  bool isBusy() override { return false; }
  Sector_t sectorCount() override { return 1024 * 1024; }
  bool syncDevice() override { return !dead; }
  bool readSector(Sector_t n, uint8_t* out) override {
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
  Bytes old{}, update{}, later{}, progress{};
  old.fill(17);
  update.fill(34);
  later.fill(51);
  progress.fill(68);
  const char* progressPath = nested ? "/crosspoint/progress.bin" : "/progress.bin";
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    if (nested) assert(volume.mkdir("/crosspoint/reading_stats/epub_123", true));
    if (!initial) assert(writeFile(primary.c_str(), old));
    assert(writeFile(progressPath, progress));
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
      Bytes actualProgress{};
      assert(readFile(progressPath, actualProgress) && actualProgress == progress);
      assert(writeFile("/later.bin", later));
      FsVolume after;
      assert(after.begin(&disk));
      Storage.attach(after);
      assert(readFile(progressPath, actualProgress) && actualProgress == progress);
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
int main() {
  for (bool exfat : {false, true})
    for (bool initial : {false, true})
      for (bool nested : {false, true}) exercise(exfat, initial, nested);
}
