#include <FatLib/FatFile.h>
#include <FatLib/FatFormatter.h>
#include <FatLib/FatVolume.h>
#include <common/FsBlockDeviceInterface.h>

#include <cassert>
#ifdef NDEBUG
#error "This probe requires assertions enabled"
#endif
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct Disk : FsBlockDeviceInterface {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(64 * 1024 * 1024);
  int writes = 0, failAt = -1;
  bool dead = false, injected = false;
  bool isBusy() override { return false; }
  Sector_t sectorCount() override { return bytes.size() / 512; }
  bool syncDevice() override { return !dead; }
  bool readSector(Sector_t n, uint8_t* out) override {
    if (dead) return false;
    assert(n < sectorCount());
    std::memcpy(out, bytes.data() + n * 512, 512);
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
      injected = dead = true;
      return false;
    }
    assert(n < sectorCount());
    std::memcpy(bytes.data() + n * 512, in, 512);
    return true;
  }
  bool writeSectors(Sector_t n, const uint8_t* in, size_t count) override {
    for (size_t i = 0; i < count; ++i)
      if (!writeSector(n + i, in + 512 * i)) return false;
    return true;
  }
};

std::vector<uint8_t> payload(size_t size, uint8_t seed) {
  std::vector<uint8_t> result(size);
  for (size_t i = 0; i < size; ++i) result[i] = static_cast<uint8_t>(seed + i * 13);
  return result;
}
int inspect(FatVolume&, const char* name, Cluster_t& cluster, const std::vector<uint8_t>& expected) {
  FatFile file;
  if (!file.open(name, O_RDONLY) || !file.isFile()) return 0;
  cluster = file.firstCluster();
  std::vector<uint8_t> bytes(expected.size());
  const bool valid = file.fileSize() == expected.size() &&
                     file.read(bytes.data(), bytes.size()) == static_cast<int>(bytes.size()) && bytes == expected;
  file.close();
  return valid ? 2 : 1;
}
int main() {
  const auto device = payload(159, 11), book = payload(73, 37), prior = payload(159, 83), later = payload(73, 129);
  Disk baseline;
  uint8_t scratch[512];
  FatFormatter formatter;
  assert(formatter.format(&baseline, scratch));
  {
    FatVolume volume;
    assert(volume.begin(&baseline));
    FatFile root, cp, reading, bookSource, backup, books, leaf, file;
    assert(root.openRoot(&volume));
    assert(cp.mkdir(&root, ".crosspoint"));
    assert(file.open(&cp, "global_stats.bin", O_CREAT | O_EXCL | O_WRONLY));
    assert(file.write(device.data(), device.size()) == device.size());
    assert(file.close());
    assert(reading.mkdir(&cp, "reading_stats"));
    assert(bookSource.mkdir(&reading, "epub_123"));
    assert(file.open(&bookSource, "stats_v5.bin", O_CREAT | O_EXCL | O_WRONLY));
    assert(file.write(book.data(), book.size()) == book.size());
    assert(file.close());
    assert(backup.mkdir(&root, ".crossink-stats-backup"));
    assert(file.open(&backup, "prior.bin", O_CREAT | O_EXCL | O_WRONLY));
    assert(file.write(prior.data(), prior.size()) == prior.size());
    assert(file.close());
    assert(books.mkdir(&backup, "books"));
    assert(leaf.mkdir(&books, "epub_123"));
    assert(file.open(&leaf, "prior.bin", O_CREAT | O_EXCL | O_WRONLY));
    assert(file.write(prior.data(), prior.size()) == prior.size());
    assert(file.close());
    assert(leaf.close());
    assert(books.close());
    assert(backup.close());
    assert(bookSource.close());
    assert(reading.close());
    assert(cp.close());
    assert(root.close());
    assert(volume.cacheClear());
  }
  int injectedTotal = 0;
  for (int scenario = 0; scenario < 2; ++scenario) {
    const auto& expected = scenario == 0 ? device : book;
    const char* source =
        scenario == 0 ? "/.crosspoint/global_stats.bin" : "/.crosspoint/reading_stats/epub_123/stats_v5.bin";
    const char* relative = scenario == 0
                               ? ".crossink-stats-backup/stats_cp_0000000008_M_00000000_000000_0123456789abcdef.bin"
                               : ".crossink-stats-backup/books/epub_123/book_0000000008_0123456789abcdef.bin";
    const std::string target = std::string("/") + relative;
    const char* priorPath =
        scenario == 0 ? "/.crossink-stats-backup/prior.bin" : "/.crossink-stats-backup/books/epub_123/prior.bin";
    const char* followPath =
        scenario == 0 ? "/.crossink-stats-backup/later.bin" : "/.crossink-stats-backup/books/epub_123/later.bin";
    int injected = 0;
    for (int f = -1; f < 64; ++f) {
      Disk disk;
      disk.bytes = baseline.bytes;
      {
        FatVolume volume;
        assert(volume.begin(&disk));
        FatFile root, file;
        assert(root.openRoot(&volume));
        disk.writes = 0;
        disk.failAt = f;
        const bool created = file.open(&root, relative, O_CREAT | O_EXCL | O_WRONLY);
        if (created) {
          file.write(expected.data(), expected.size());
          file.close();
        }
        if (f < 0) assert(created);
        if (!disk.dead) {
          root.close();
          volume.cacheClear();
        }
      }
      if (f >= 0 && !disk.injected) break;
      if (disk.injected) ++injected;
      disk.dead = false;
      disk.failAt = -1;
      FatVolume reopened;
      if (!reopened.begin(&disk)) {
        std::printf("scenario=%d fault=%d remount=failed\n", scenario, f);
        return 2;
      }
      Cluster_t sourceCluster = 0, priorCluster = 0, targetCluster = 0;
      const int sourceState = inspect(reopened, source, sourceCluster, expected);
      const int priorState = inspect(reopened, priorPath, priorCluster, prior);
      const int targetState = inspect(reopened, target.c_str(), targetCluster, expected);
      if (sourceState != 2 || priorState != 2 ||
          (targetCluster && (targetCluster == sourceCluster || targetCluster == priorCluster)) ||
          (f < 0 && targetState != 2)) {
        std::printf("scenario=%d fault=%d source=%d prior=%d target=%d\n", scenario, f, sourceState, priorState,
                    targetState);
        return 3;
      }
      FatFile follow;
      if (!follow.open(followPath, O_CREAT | O_EXCL | O_WRONLY) ||
          follow.write(later.data(), later.size()) != later.size() || !follow.close()) {
        std::printf("scenario=%d fault=%d follow-on=failed\n", scenario, f);
        return 4;
      }
      if (!reopened.cacheClear()) return 5;
      FatVolume after;
      if (!after.begin(&disk)) return 6;
      sourceCluster = priorCluster = 0;
      Cluster_t laterCluster = 0;
      if (inspect(after, source, sourceCluster, expected) != 2 || inspect(after, priorPath, priorCluster, prior) != 2 ||
          inspect(after, followPath, laterCluster, later) != 2 || laterCluster == sourceCluster ||
          laterCluster == priorCluster)
        return 7;
      if (targetState == 2) {
        targetCluster = 0;
        if (inspect(after, target.c_str(), targetCluster, expected) != 2 || targetCluster == laterCluster) return 8;
      }
    }
    std::printf("scenario=%s injected=%d\n", scenario == 0 ? "device" : "book", injected);
    if (injected == 0) return 1;
    injectedTotal += injected;
  }
  std::printf("injected write failures total=%d; source, prior backup and later file read back after remount\n",
              injectedTotal);
}
