#include <Arduino.h>
#include <DeviceStatsBackup.h>
#include <ExFatLib/ExFatFormatter.h>
#include <FatLib/FatFormatter.h>
#include <FsLib/FsVolume.h>
#include <GlobalReadingStats.h>
#include <HalStorage.h>
#include <StatsAccessGate.h>
#include <StatsSafeReplace.h>
#include <StatsSessionLease.h>
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

namespace readingstats {
bool normalStatsAllowed() { return true; }
}  // namespace readingstats
bool StatsSessionLease::anyActive() { return false; }
bool ReadingStatsDate::isValid() const { return year >= 1 && month >= 1 && month <= 12 && day >= 1 && day <= 31; }
constexpr char PRIMARY[] = "/.crosspoint/global_stats.bin";
using Bytes = std::array<uint8_t, GlobalReadingStats::CURRENT_FILE_SIZE>;
bool GlobalReadingStats::encode(Bytes& data) const {
  if (!writable) return false;
  data = {};
  data[0] = CURRENT_FILE_VERSION;
  for (int i = 0; i < 4; ++i) data[5 + i] = static_cast<uint8_t>(totalReadingSeconds >> (8 * i));
  return true;
}
bool GlobalReadingStats::decode(const uint8_t* data, size_t size, GlobalReadingStats& result) {
  if (!data || size != CURRENT_FILE_SIZE || data[0] != CURRENT_FILE_VERSION) return false;
  GlobalReadingStats decoded;
  for (int i = 0; i < 4; ++i) decoded.totalReadingSeconds |= uint32_t(data[5 + i]) << (8 * i);
  result = decoded;
  return true;
}
GlobalReadingStats GlobalReadingStats::loadReadOnly() {
  GlobalReadingStats result;
  HalFile file;
  if (!Storage.openFileForRead("TEST", PRIMARY, file)) {
    result.writable = false;
    return result;
  }
  Bytes bytes{};
  bool ok = file.fileSize() == bytes.size() && file.read(bytes.data(), bytes.size()) == int(bytes.size());
  ok = file.close() && ok;
  if (!ok || !decode(bytes.data(), bytes.size(), result)) result.writable = false;
  return result;
}
bool GlobalReadingStats::save() const {
  Bytes bytes{};
  return encode(bytes) && statsreplace::save("TEST", PRIMARY, bytes.data(), bytes.size(), true);
}
uint64_t hash(const Bytes& bytes) {
  uint64_t h = 14695981039346656037ULL;
  for (auto b : bytes) h = (h ^ b) * 1099511628211ULL;
  return h;
}
std::string backupName(uint32_t millisValue, const Bytes& bytes) {
  char name[80];
  snprintf(name, sizeof(name), "stats_cp_%010lu_M_00000000_000000_%016llx.bin", (unsigned long)(millisValue + 1),
           (unsigned long long)hash(bytes));
  return name;
}
bool matches(const std::string& path, const Bytes& expected) {
  HalFile f;
  if (!Storage.openFileForRead("TEST", path, f)) return false;
  Bytes got{};
  bool ok = f.fileSize() == got.size() && f.read(got.data(), got.size()) == int(got.size());
  return f.close() && ok && got == expected;
}
void exercise(bool exfat, bool failStop, bool legacy = false) {
  Disk baseline;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter f;
    assert(f.format(&baseline, scratch));
  } else {
    FatFormatter f;
    assert(f.format(&baseline, scratch));
  }
  GlobalReadingStats saved, prior;
  saved.totalReadingSeconds = 1000;
  prior.totalReadingSeconds = 2000;
  Bytes selectedBytes{}, previousBytes{};
  assert(saved.encode(selectedBytes) && prior.encode(previousBytes));
  const std::string sourceName = legacy ? "stats_2026-11-10.bin" : backupName(7, selectedBytes);
  const std::string beforeName = backupName(8, previousBytes);
  const std::string source = "/.crossink-stats-backup/" + sourceName, before = "/.crossink-stats-backup/" + beforeName;
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    assert(volume.mkdir("/.crosspoint", true));
    assert(saved.save());
    testMillis = 7;
    if (legacy) {
      assert(volume.mkdir("/.crossink-stats-backup", true));
      auto file = Storage.open(source.c_str(), O_CREAT | O_EXCL | O_WRONLY);
      assert(file && file.write(selectedBytes.data(), selectedBytes.size()) == selectedBytes.size() && file.sync() &&
             file.close());
    } else
      assert(statsbackup::createDeviceBackup(saved));
    assert(prior.save());
  }
  int injected = 0;
  for (int fault = -1; fault < 500; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    bool restored = false;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      testMillis = 8;
      disk.writes = 0;
      disk.failAt = fault;
      disk.failStop = failStop;
      auto result = statsbackup::restoreDeviceBackup(sourceName, previousBytes.data(), previousBytes.size(),
                                                     selectedBytes.data(), selectedBytes.size());
      restored = result == statsbackup::RestoreResult::Restored;
    }
    if (fault >= 0 && !disk.injected) break;
    if (fault == -1) assert(restored);
    if (disk.injected) ++injected;
    disk.dead = false;
    disk.failAt = -1;
    FsVolume after;
    assert(after.begin(&disk));
    Storage.attach(after);
    bool selected = matches(source, selectedBytes);
    bool original = matches(PRIMARY, previousBytes) || matches(std::string(PRIMARY) + ".bak", previousBytes) ||
                    matches(before, previousBytes);
    if (!selected || !original || (restored && !matches(PRIMARY, selectedBytes))) {
      std::fprintf(stderr, "failure %s stop=%d fault=%d restored=%d selected=%d prior=%d\n", exfat ? "exfat" : "fat",
                   failStop, fault, restored, selected, original);
      std::abort();
    }
    const bool priorPrimary = matches(PRIMARY, previousBytes),
               priorJournal = matches(std::string(PRIMARY) + ".bak", previousBytes);
    const bool priorSnapshot = matches(before, previousBytes);
    auto later = Storage.open("/later.bin", O_CREAT | O_EXCL | O_WRONLY);
    assert(later);
    std::array<uint8_t, 4096> content{};
    content.fill(0xa5);
    assert(later.write(content.data(), content.size()) == content.size() && later.sync() && later.close());
    FsVolume remounted;
    assert(remounted.begin(&disk));
    Storage.attach(remounted);
    assert(matches(source, selectedBytes));
    assert((priorPrimary && matches(PRIMARY, previousBytes)) ||
           (priorJournal && matches(std::string(PRIMARY) + ".bak", previousBytes)) ||
           (priorSnapshot && matches(before, previousBytes)));
  }
  std::printf("%s %s %s: %d injected write failures preserved selected and prior snapshots\n", exfat ? "exfat" : "fat",
              failStop ? "fail-stop" : "transient", legacy ? "legacy" : "checked", injected);
  assert(injected > 0);
}
void exerciseReset(bool exfat, bool failStop) {
  Disk baseline;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter f;
    assert(f.format(&baseline, scratch));
  } else {
    FatFormatter f;
    assert(f.format(&baseline, scratch));
  }
  GlobalReadingStats prior;
  prior.totalReadingSeconds = 2000;
  Bytes previous{}, cleared{};
  assert(prior.encode(previous) && GlobalReadingStats{}.encode(cleared));
  const std::string before = "/.crossink-stats-backup/" + backupName(8, previous);
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    assert(volume.mkdir("/.crosspoint", true));
    assert(prior.save());
    assert(volume.mkdir("/.crossink-stats-backup", true));
  }
  int injected = 0;
  for (int fault = -1; fault < 500; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    bool reset = false;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      testMillis = 8;
      disk.writes = 0;
      disk.failAt = fault;
      disk.failStop = failStop;
      reset = statsbackup::resetDeviceStats(previous.data(), previous.size()) == statsbackup::RestoreResult::Restored;
    }
    if (fault >= 0 && !disk.injected) break;
    if (fault == -1) assert(reset);
    if (disk.injected) ++injected;
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    const bool priorPrimary = matches(PRIMARY, previous),
               priorJournal = matches(std::string(PRIMARY) + ".bak", previous);
    const bool priorSnapshot = matches(before, previous);
    assert(priorPrimary || priorJournal || priorSnapshot);
    if (reset) assert(priorSnapshot && matches(PRIMARY, cleared));
    auto later = Storage.open("/later.bin", O_CREAT | O_EXCL | O_WRONLY);
    assert(later);
    std::array<uint8_t, 4096> content{};
    content.fill(0xa5);
    assert(later.write(content.data(), content.size()) == content.size() && later.sync() && later.close());
    FsVolume remounted;
    assert(remounted.begin(&disk));
    Storage.attach(remounted);
    assert((priorPrimary && matches(PRIMARY, previous)) ||
           (priorJournal && matches(std::string(PRIMARY) + ".bak", previous)) ||
           (priorSnapshot && matches(before, previous)));
  }
  std::printf("%s %s reset: %d injected write failures preserved prior totals\n", exfat ? "exfat" : "fat",
              failStop ? "fail-stop" : "transient", injected);
  assert(injected > 0);
}
void exerciseDaily(bool exfat, bool failStop) {
  Disk baseline;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter f;
    assert(f.format(&baseline, scratch));
  } else {
    FatFormatter f;
    assert(f.format(&baseline, scratch));
  }
  GlobalReadingStats prior;
  prior.totalReadingSeconds = 2000;
  Bytes previous{};
  assert(prior.encode(previous));
  char filename[80];
  snprintf(filename, sizeof(filename), "/.crossink-stats-backup/stats_cp_20261110_A_%016llx.bin",
           (unsigned long long)hash(previous));
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    assert(volume.mkdir("/.crosspoint", true));
    assert(prior.save());
    assert(volume.mkdir("/.crossink-stats-backup", true));
  }
  int injected = 0;
  for (int fault = -1; fault < 500; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    bool saved = false;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      disk.writes = 0;
      disk.failAt = fault;
      disk.failStop = failStop;
      saved = statsbackup::createDailyDeviceBackup({2026, 11, 10}) == statsbackup::AutomaticBackupResult::Saved;
    }
    if (fault >= 0 && !disk.injected) break;
    if (fault == -1) assert(saved);
    if (disk.injected) ++injected;
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    assert(matches(PRIMARY, previous));
    if (saved) assert(matches(filename, previous));
    auto later = Storage.open("/later.bin", O_CREAT | O_EXCL | O_WRONLY);
    assert(later);
    std::array<uint8_t, 4096> content{};
    content.fill(0xa5);
    assert(later.write(content.data(), content.size()) == content.size() && later.sync() && later.close());
    FsVolume remounted;
    assert(remounted.begin(&disk));
    Storage.attach(remounted);
    assert(matches(PRIMARY, previous));
    if (saved) assert(matches(filename, previous));
  }
  std::printf("%s %s automatic: %d injected write failures preserved totals\n", exfat ? "exfat" : "fat",
              failStop ? "fail-stop" : "transient", injected);
  assert(injected > 0);
}
int main() {
  for (bool exfat : {false, true})
    for (bool failStop : {false, true}) {
      exercise(exfat, failStop);
      exercise(exfat, failStop, true);
      exerciseReset(exfat, failStop);
      exerciseDaily(exfat, failStop);
    }
}
