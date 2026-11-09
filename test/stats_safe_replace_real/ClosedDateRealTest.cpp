#include <ExFatLib/ExFatFormatter.h>
#include <FatLib/FatFormatter.h>
#include <FsLib/FsVolume.h>
#include <HalStorage.h>
#include <ReadingStatsStore.h>
#include <common/FsBlockDeviceInterface.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <unordered_map>

struct Disk : FsBlockDeviceInterface {
  std::unordered_map<Sector_t, std::array<uint8_t, 512>> sectors;
  int reads = 0, writes = 0, failAt = -1, failReadAt = -1;
  bool injected = false, readInjected = false, dead = false, failStop = false;
  bool isBusy() override { return false; }
  Sector_t sectorCount() override { return 1024 * 1024; }
  bool syncDevice() override { return true; }
  bool readSector(Sector_t sector, uint8_t* out) override {
    if (dead) return false;
    if (reads++ == failReadAt) {
      readInjected = true;
      dead = failStop;
      Storage.noteIoFailure();
      return false;
    }
    auto it = sectors.find(sector);
    if (it == sectors.end())
      std::memset(out, 0, 512);
    else
      std::memcpy(out, it->second.data(), 512);
    return true;
  }
  bool readSectors(Sector_t sector, uint8_t* out, size_t count) override {
    for (size_t n = 0; n < count; ++n)
      if (!readSector(sector + n, out + n * 512)) return false;
    return true;
  }
  bool writeSector(Sector_t sector, const uint8_t* data) override {
    if (dead) return false;
    if (writes++ == failAt) {
      injected = true;
      dead = failStop;
      Storage.noteIoFailure();
      if (traceDateIo) std::fprintf(stderr, "TRACE injected write fault\n");
      return false;
    }
    if (traceDateIo) std::fprintf(stderr, "TRACE write offset=%d after-fault=%d\n", writes - 1, injected);
    std::memcpy(sectors[sector].data(), data, 512);
    return true;
  }
  bool writeSectors(Sector_t sector, const uint8_t* data, size_t count) override {
    for (size_t n = 0; n < count; ++n)
      if (!writeSector(sector + n, data + n * 512)) return false;
    return true;
  }
};

int main() {
  int inaccessible = 0, recovered = 0;
  constexpr const char* cache = "/.crosspoint/epub_123";
  constexpr const char* book = "/Books/test.epub";
  const std::string directory = readingstats::bookDirectory(cache);
  for (bool exfat : {false, true}) {
    Disk baseline;
    uint8_t scratch[512]{};
    if (exfat) {
      ExFatFormatter f;
      assert(f.format(&baseline, scratch));
    } else {
      FatFormatter f;
      assert(f.format(&baseline, scratch));
    }
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    Storage.resetMonitor();
    assert(volume.mkdir("/.crosspoint/reading_stats", true));
    BookReadingStats original;
    original.totalReadingSeconds = 1240;
    original.totalPagesTurned = 40;
    assert(readingstats::saveBook(directory, original));
    auto progress = Storage.open("/.crosspoint/progress.bin", O_CREAT | O_EXCL | O_WRONLY);
    const uint8_t state[] = {1, 2, 3, 4};
    assert(progress && progress.write(state, sizeof(state)) == sizeof(state) && progress.close());
    {
      Disk disk;
      disk.sectors = baseline.sectors;
      FsVolume mounted;
      assert(mounted.begin(&disk));
      Storage.attach(mounted);
      Storage.noteIoFailure();
      const int reads = disk.reads, writes = disk.writes;
      const auto blocked = BookReadingStats::load(directory, BookReadingStats::LoadMode::ReadOnly);
      assert(!blocked.writable && !blocked.recoveryBackup && disk.reads == reads && disk.writes == writes);
    }
    for (bool failStop : {false, true}) {
      int injected = 0;
      for (int fault = -1; fault < 64; ++fault) {
        Disk disk;
        disk.sectors = baseline.sectors;
        disk.failStop = failStop;
        FsVolume mounted;
        assert(mounted.begin(&disk));
        Storage.attach(mounted);
        Storage.resetMonitor();
        bool tracking = false, found = false, known = false;
        auto snapshot =
            readingstats::loadBook(cache, book, tracking, &found, BookReadingStats::LoadMode::ReadOnly, &known);
        assert(found && known && snapshot.writable && snapshot.totalReadingSeconds == 1240);
        disk.writes = 0;
        disk.failAt = fault;
        traceDateIo = exfat && !failStop && fault == 7;
        const bool saved = readingstats::editClosedBookDate(cache, book, true, {2024, 2, 29}, snapshot);
        traceDateIo = false;
        if (fault >= 0 && !disk.injected) break;
        if (disk.injected) {
          ++injected;
          assert(!saved && !snapshot.startDateManual);
        } else {
          assert(saved && snapshot.startDateManual && snapshot.startDate.day == 29);
        }
        disk.failAt = -1;
        disk.dead = false;
        FsVolume after;
        assert(after.begin(&disk));
        Storage.attach(after);
        auto preserved = Storage.open("/.crosspoint/progress.bin");
        uint8_t check[sizeof(state)]{};
        assert(preserved && preserved.read(check, sizeof(check)) == sizeof(check) &&
               std::memcmp(check, state, sizeof(state)) == 0 && preserved.close());
        Storage.resetMonitor();
        bool enabled = false, stillFound = false;
        const int readsBeforeLoadWrites = disk.writes;
        const auto result =
            readingstats::loadBook(cache, book, enabled, &stillFound, BookReadingStats::LoadMode::ReadOnly);
        assert(disk.writes == readsBeforeLoadWrites);
        if (result.recoveryBackup) {
          ++recovered;
          assert(!saved && !result.writable && result.totalReadingSeconds == 1240 && result.totalPagesTurned == 40);
          assert(Storage.exists((directory + "/stats_v5.bin").c_str()));
          assert(Storage.exists((directory + "/stats_v5.bin.tmp").c_str()));
          assert(Storage.exists((directory + "/stats_v5.bin.bak").c_str()));
        }
        if (!stillFound || result.totalReadingSeconds != 1240 || result.totalPagesTurned != 40) {
          ++inaccessible;
          bool validArtifact = false;
          std::fprintf(
              stderr,
              "failure fs=%s fail_stop=%d offset=%d injected=%d saved=%d found=%d writable=%d seconds=%u pages=%u\n",
              exfat ? "exfat" : "fat", failStop, fault, disk.injected, saved, stillFound, result.writable,
              result.totalReadingSeconds, result.totalPagesTurned);
          for (const char* suffix : {"", ".tmp", ".bak"}) {
            const auto name = directory + "/stats_v5.bin" + suffix;
            auto item = Storage.open(name.c_str());
            if (!item) {
              std::fprintf(stderr, "artifact %s missing\n", suffix);
              continue;
            }
            uint8_t bytes[BookReadingStats::CURRENT_FILE_SIZE]{};
            const auto size = item.fileSize();
            const int n = item.read(bytes, sizeof(bytes));
            std::array<uint8_t, BookReadingStats::CURRENT_FILE_SIZE> old{}, updated{};
            assert(original.encode(old.data(), old.size()));
            auto changed = original;
            changed.startDate = {2024, 2, 29};
            changed.startDateManual = true;
            assert(changed.encode(updated.data(), updated.size()));
            validArtifact |= n == int(old.size()) && (std::memcmp(bytes, old.data(), old.size()) == 0 ||
                                                      std::memcmp(bytes, updated.data(), updated.size()) == 0);
            std::fprintf(stderr, "artifact %s size=%zu read=%d old=%d new=%d\n", suffix, size, n,
                         n == int(old.size()) && std::memcmp(bytes, old.data(), old.size()) == 0,
                         n == int(updated.size()) && std::memcmp(bytes, updated.data(), updated.size()) == 0);
            item.close();
          }
          assert(validArtifact && !saved);
        }
      }
      assert(injected > 0);
      std::printf("%s fail_stop=%d closed-date injected writes=%d progress preserved\n", exfat ? "exFAT" : "FAT",
                  failStop, injected);
    }

    // Construct the exact three-file recovery state, then fail individual reads.
    Storage.attach(volume);
    Storage.resetMonitor();
    const std::string primary = directory + "/stats_v5.bin";
    assert(Storage.rename(primary.c_str(), (primary + ".bak").c_str()));
    auto empty = Storage.open(primary.c_str(), O_CREAT | O_EXCL | O_WRONLY);
    assert(empty && empty.close());
    auto pending = Storage.open((primary + ".tmp").c_str(), O_CREAT | O_EXCL | O_WRONLY);
    std::array<uint8_t, BookReadingStats::CURRENT_FILE_SIZE> bytes{};
    assert(original.encode(bytes.data(), bytes.size()));
    assert(pending && pending.write(bytes.data(), bytes.size()) == bytes.size() && pending.close());
    {
      Disk disk;
      disk.sectors = baseline.sectors;
      FsVolume mounted;
      assert(mounted.begin(&disk));
      Storage.attach(mounted);
      Storage.resetMonitor();
      auto writer = Storage.open("/.crosspoint/failed-writer.bin", O_CREAT | O_EXCL | O_WRONLY);
      const uint8_t value = 42;
      assert(writer && writer.write(&value, 1) == 1);
      disk.failAt = disk.writes;
      assert(!writer.sync() && disk.injected);
      disk.failAt = -1;
      const int reads = disk.reads, writes = disk.writes;
      const auto blocked = BookReadingStats::load(directory, BookReadingStats::LoadMode::ReadOnly);
      assert(!blocked.writable && !blocked.recoveryBackup && disk.reads == reads && disk.writes == writes);
      const bool closed = writer.close();
      std::printf("%s failed-writer loader_no_io=1 close=%d later_writes=%d\n", exfat ? "exFAT" : "FAT", closed,
                  disk.writes - writes);
    }
    for (bool failStop : {false, true}) {
      int injected = 0;
      for (int fault = -1; fault < 64; ++fault) {
        Disk disk;
        disk.sectors = baseline.sectors;
        disk.failStop = failStop;
        FsVolume mounted;
        assert(mounted.begin(&disk));
        Storage.attach(mounted);
        Storage.resetMonitor();
        disk.reads = 0;
        disk.failReadAt = fault;
        const int writes = disk.writes;
        const auto result = BookReadingStats::load(directory, BookReadingStats::LoadMode::ReadOnly);
        if (fault >= 0 && !disk.readInjected) break;
        assert(disk.writes == writes);
        if (disk.readInjected) {
          ++injected;
          assert(!result.writable && !result.recoveryBackup);
        } else {
          assert(!result.writable && result.recoveryBackup && result.totalReadingSeconds == 1240);
        }
        disk.failReadAt = -1;
        disk.dead = false;
        FsVolume after;
        assert(after.begin(&disk));
        Storage.attach(after);
        auto preserved = Storage.open("/.crosspoint/progress.bin");
        uint8_t check[sizeof(state)]{};
        assert(preserved && preserved.read(check, sizeof(check)) == sizeof(check) &&
               std::memcmp(check, state, sizeof(state)) == 0 && preserved.close());
        for (const char* suffix : {"", ".tmp", ".bak"}) assert(Storage.exists((primary + suffix).c_str()));
      }
      assert(injected > 0);
      std::printf("%s fail_stop=%d recovery injected reads=%d artifacts preserved\n", exfat ? "exFAT" : "FAT", failStop,
                  injected);
    }
  }
  if (inaccessible) std::fprintf(stderr, "inaccessible statistics cases=%d\n", inaccessible);
  std::printf("read-only backup cases=%d\n", recovered);
  assert(recovered > 0);
  return inaccessible ? 1 : 0;
}
