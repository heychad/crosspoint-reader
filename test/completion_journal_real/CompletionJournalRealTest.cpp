#ifdef NDEBUG
#error "This test requires assertions"
#endif
#include <CompletionJournalIO.h>
#include <CompletionMoveFile.h>
#include <CompletionMovePlanIO.h>
#include <CompletionMoveReferences.h>
#include <CrossPointState.h>
#include <DeferredCompletionIntentIO.h>
#include <DeferredCompletionMove.h>
#include <ExFatLib/ExFatFormatter.h>
#include <FatLib/FatFormatter.h>
#include <FsLib/FsVolume.h>
#include <HalStorage.h>
#include <MoveReferenceSafeWrite.h>
#include <RecentBooksStore.h>
#if defined(SDFAT_REAL_MOVE_STORES)
#include <CompletionTransaction.h>
#include <ReadingStatsUtils.h>
#include <StatsSessionLease.h>

#include "MovedReadingState.h"
#endif
#if defined(SDFAT_IMPORT_SNAPSHOT)
#include <CrossInkImportSnapshot.h>
#endif
#if defined(SDFAT_IMPORT_TRANSACTION)
#include <CrossInkImportGuard.h>
#include <CrossInkImportTransaction.h>
#endif
#include <common/FsBlockDeviceInterface.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>

#if defined(SDFAT_ALIAS_ONE_SECTOR)
#include "AliasLocatorRecord.h"
#endif
#if defined(SDFAT_CLEANUP_EXPERIMENT)
#if defined(SDFAT_CLEANUP_INPLACE)
#include "CleanupInplaceExperiment.h"
#else
#include "CleanupExperiment.h"
#endif
#endif

struct Disk : FsBlockDeviceInterface {
  std::unordered_map<Sector_t, std::array<uint8_t, 512>> sectors;
  Sector_t sectorsTotal = 1024 * 1024;
  Sector_t failReadSector = static_cast<Sector_t>(-1);
  bool injectedRead = false;
  int reads = 0;
  int failReadAt = -1;
  int writes = 0, failAt = -1;
  int tornAfter = -1;
  bool injected = false, dead = false, failStop = false;
  bool isBusy() override { return false; }
  Sector_t sectorCount() override { return sectorsTotal; }
  bool syncDevice() override { return !dead; }
  bool readSector(Sector_t n, uint8_t* out) override {
    if (reads++ == failReadAt || n == failReadSector) {
      failReadSector = static_cast<Sector_t>(-1);
      failReadAt = -1;
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
      if (tornAfter >= 0) {
        auto& sector = sectors[n];
        std::memcpy(sector.data(), in, std::min<size_t>(static_cast<size_t>(tornAfter), sector.size()));
      }
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

namespace cj = readingstats::completionjournal;
constexpr char FIXTURE[] = "/.crosspoint/progress.bin";
constexpr char SLOT[] = "/.completion-journal/.completion-00";

void exercise(bool exfat, bool failStop) {
  Disk baseline;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  } else {
    FatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  }
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    assert(volume.mkdir("/.crosspoint/reading_stats", true));
    assert(volume.mkdir("/.completion-journal", true));
    auto file = Storage.open(FIXTURE, O_CREAT | O_EXCL | O_WRONLY);
    assert(file);
    const uint8_t bytes[] = {12, 34, 56, 78};
    assert(file.write(bytes, sizeof(bytes)) == sizeof(bytes));
    assert(file.sync());
    assert(file.close());
  }
  cj::Record record{};
  cj::Book book{};
  cj::Global global{};
  book.fill(23);
  global.fill(42);
  book[0] = 5;
  book[11] = 1;
  book[16] = 0;
  for (unsigned i = 17; i < 25; ++i) book[i] = 0;
  global[0] = 3;
  cj::Baseline baselineSnapshots{};
  baselineSnapshots.book = book;
  baselineSnapshots.global = global;
  baselineSnapshots.book[11] = 0;
  baselineSnapshots.global[13] = 0;
  baselineSnapshots.bookSize = cj::BOOK_BYTES;
  baselineSnapshots.globalSize = cj::GLOBAL_BYTES;
  size_t size = 0;
  assert(cj::encode("/.crosspoint/reading_stats/epub_123", book, global, baselineSnapshots, record, size));
  int faults = 0;
  int maxScanReads = 0;
  for (int fault = -1; fault < 80; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    cj::CreateResult result;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      disk.failAt = fault;
      disk.failStop = failStop;
      result = cj::createRecord(0, record, size);
    }
    if (fault >= 0 && !disk.injected) break;
    if (disk.injected) ++faults;
    if (fault == -1 && result != cj::CreateResult::Created) {
      std::fprintf(stderr, "unexpected create result %d\n", static_cast<int>(result));
      assert(false);
    }
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    auto fixture = Storage.open(FIXTURE, O_RDONLY);
    assert(fixture);
    uint8_t bytes[4]{};
    assert(fixture.read(bytes, sizeof(bytes)) == sizeof(bytes));
    assert(fixture.close());
    assert(bytes[0] == 12 && bytes[1] == 34 && bytes[2] == 56 && bytes[3] == 78);
    cj::Record output{};
    size_t outputSize = 0;
    const auto inspected = cj::inspectRecord(0, output, outputSize);
    cj::Record scanned{};
    size_t scannedSize = 0;
    unsigned scannedSlot = 99;
    const int beforeScan = disk.reads;
    const auto scan = cj::scanRecords(scanned, scannedSize, scannedSlot);
    maxScanReads = std::max(maxScanReads, disk.reads - beforeScan);
    if (fault == -1) assert(scan == cj::ScanResult::PreparedOnly);
    if (inspected == cj::InspectResult::Missing)
      assert(scan == cj::ScanResult::Empty || scan == cj::ScanResult::Unavailable);
    if (inspected == cj::InspectResult::Damaged)
      assert(scan == cj::ScanResult::Damaged || scan == cj::ScanResult::Unavailable);
    if (inspected == cj::InspectResult::Valid && scan != cj::ScanResult::Unavailable) {
      assert(scan == cj::ScanResult::PreparedOnly);
    }
    if (result == cj::CreateResult::Created) {
      assert(inspected == cj::InspectResult::Valid);
      assert(outputSize == size && output == record);
    }
    if (inspected == cj::InspectResult::Valid) {
      assert(outputSize == size && output == record);
      assert(cj::createRecord(0, record, size) == cj::CreateResult::Occupied);
    }
    if (inspected == cj::InspectResult::Damaged) {
      assert(cj::createRecord(0, record, size) == cj::CreateResult::Occupied);
    }
  }
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    assert(cj::createRecord(0, record, size) == cj::CreateResult::Created);
    assert(cj::createRecord(63, record, size) == cj::CreateResult::Created);
    uint64_t mask = 99;
    assert(Storage.lookupNumberedChildren("/.completion-journal", ".completion-", mask));
    assert(mask == (uint64_t{1} | (uint64_t{1} << 63)));
    mask = 99;
    assert(!Storage.lookupNumberedChildren("/.completion-journal", "../", mask) && mask == 99);
    assert(Storage.lookupNumberedChildren("/.crosspoint/absent", ".completion-", mask) && mask == 0);
  }
  int readFaults = 0;
  for (int fault = 0; fault < 128; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    disk.reads = 0;
    disk.failReadAt = fault;
    uint64_t mask = UINT64_MAX;
    const bool ok = Storage.lookupNumberedChildren("/.completion-journal", ".completion-", mask);
    if (!disk.injectedRead) break;
    ++readFaults;
    if (ok || mask != UINT64_MAX) {
      std::fprintf(stderr, "%s lookup accepted injected read failure %d, ok=%d mask=%llu\n", exfat ? "exFAT" : "FAT",
                   fault, ok, static_cast<unsigned long long>(mask));
      assert(false);
    }
  }
  assert(readFaults > 0);
  int sealFaults = 0;
  for (int fault = -1; fault < 80; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    cj::CreateResult result;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      disk.failAt = fault;
      disk.failStop = failStop;
      result = cj::createSeal(0, record, size);
    }
    if (fault >= 0 && !disk.injected) break;
    if (disk.injected) ++sealFaults;
    if (fault == -1 && result != cj::CreateResult::Created) {
      std::fprintf(stderr, "unexpected seal result %d\n", static_cast<int>(result));
      assert(false);
    }
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    cj::Record recovered{};
    size_t recoveredSize = 0;
    assert(cj::inspectRecord(0, recovered, recoveredSize) == cj::InspectResult::Valid);
    assert(recoveredSize == size && recovered == record);
    const auto seal = cj::inspectSeal(0, record, size);
    cj::Record candidate{};
    size_t candidateSize = 0;
    unsigned candidateSlot = 99;
    const auto scan = cj::scanRecords(candidate, candidateSize, candidateSlot);
    if (seal == cj::SealResult::Missing)
      assert(scan == cj::ScanResult::PreparedOnly || scan == cj::ScanResult::Unavailable);
    if (seal == cj::SealResult::Damaged) assert(scan == cj::ScanResult::Damaged || scan == cj::ScanResult::Unavailable);
    if (seal == cj::SealResult::Matching && scan != cj::ScanResult::Unavailable) {
      assert(scan == cj::ScanResult::Candidate);
      assert(candidateSlot == 0 && candidateSize == size && candidate == record);
    }
    if (result == cj::CreateResult::Created) assert(seal == cj::SealResult::Matching);
    if (fault == -1) assert(scan == cj::ScanResult::Candidate);
    if (seal == cj::SealResult::Damaged || seal == cj::SealResult::Matching)
      assert(cj::createSeal(0, record, size) == cj::CreateResult::Occupied);
    auto fixture = Storage.open(FIXTURE, O_RDONLY);
    assert(fixture);
    uint8_t bytes[4]{};
    assert(fixture.read(bytes, sizeof(bytes)) == sizeof(bytes));
    assert(fixture.close());
    assert(bytes[0] == 12 && bytes[1] == 34 && bytes[2] == 56 && bytes[3] == 78);
  }
  assert(sealFaults > 0);
  std::printf("%s %s: %d sector write faults, max scan %d sector reads\n", exfat ? "exFAT" : "FAT",
              failStop ? "fail-stop" : "transient", faults, maxScanReads);
  std::printf("%s: %d batch lookup read faults rejected\n", exfat ? "exFAT" : "FAT", readFaults);
  std::printf("%s %s: %d seal write faults\n", exfat ? "exFAT" : "FAT", failStop ? "fail-stop" : "transient",
              sealFaults);
}

void replayFaults(bool exfat, bool failStop) {
  constexpr char bookPath[] = "/.crosspoint/reading_stats/epub_123/stats_v5.bin";
  constexpr char globalPath[] = "/.crosspoint/global_stats.bin";
  Disk baseline;
  Disk startBaseline;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  } else {
    FatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  }
  cj::Baseline previous{};
  previous.book[0] = 5;
  previous.global[0] = 3;
  previous.bookSize = cj::BOOK_BYTES;
  previous.globalSize = cj::GLOBAL_BYTES;
  cj::Book book = previous.book;
  cj::Global global = previous.global;
  // A restored book can change reading counters and history in addition to
  // completion. Exercise the journal with a distinct full book snapshot.
  book[1] = 7;
  book[3] = 0x34;
  book[4] = 0x12;
  book[7] = 42;
  book[12] = 9;
  book[25] = 17;
  book[41] = 29;
  book[69] = 31;
  book[11] = 1;
  global[13] = 1;
  cj::Record record{};
  size_t size = 0;
  assert(cj::encode("/.crosspoint/reading_stats/epub_123", book, global, previous, record, size));
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    assert(volume.mkdir("/.crosspoint/reading_stats/epub_123", true));
    assert(volume.mkdir("/.completion-journal", true));
    const auto write = [](const char* path, const uint8_t* data, size_t length) {
      auto file = Storage.open(path, O_CREAT | O_EXCL | O_WRONLY);
      assert(file && file.write(data, length) == length && file.sync() && file.close());
    };
    write(bookPath, previous.book.data(), previous.bookSize);
    write(globalPath, previous.global.data(), previous.globalSize);
    const uint8_t progress[] = {12, 34, 56, 78};
    write(FIXTURE, progress, sizeof(progress));
    startBaseline.sectors = baseline.sectors;
    assert(cj::createRecord(0, record, size) == cj::CreateResult::Created);
    assert(cj::createSeal(0, record, size) == cj::CreateResult::Created);
  }
  int startFaults = 0;
  for (int fault = -1; fault < 80; ++fault) {
    Disk disk;
    disk.sectors = startBaseline.sectors;
    cj::StartResult result;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      disk.failAt = fault;
      disk.failStop = failStop;
      result = cj::start(record, size);
    }
    if (fault >= 0 && !disk.injected) break;
    if (disk.injected) ++startFaults;
    if (fault == -1) assert(result == cj::StartResult::Started);
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    auto check = [](const char* path, const uint8_t* expected, size_t length) {
      auto file = Storage.open(path, O_RDONLY);
      assert(file);
      std::array<uint8_t, cj::GLOBAL_BYTES> bytes{};
      assert(file.fileSize() == length && file.read(bytes.data(), length) == static_cast<int>(length));
      assert(file.close() && std::memcmp(bytes.data(), expected, length) == 0);
    };
    check(bookPath, previous.book.data(), previous.bookSize);
    check(globalPath, previous.global.data(), previous.globalSize);
    const uint8_t progress[] = {12, 34, 56, 78};
    check(FIXTURE, progress, sizeof(progress));
    cj::Record candidate{};
    size_t candidateSize = 0;
    unsigned candidateSlot = 99;
    const auto state = cj::scanRecords(candidate, candidateSize, candidateSlot);
    if (result == cj::StartResult::Started) {
      assert(state == cj::ScanResult::Candidate);
      assert(candidateSlot == 0 && candidateSize == size && candidate == record);
    }
    assert(state == cj::ScanResult::Empty || state == cj::ScanResult::PreparedOnly ||
           state == cj::ScanResult::Candidate || state == cj::ScanResult::Damaged ||
           state == cj::ScanResult::Unavailable);
  }
  assert(startFaults > 0);
  int faults = 0;
  for (int fault = -1; fault < 100; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    cj::ReplayResult result;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      disk.failAt = fault;
      disk.failStop = failStop;
      result = cj::replay();
    }
    if (fault >= 0 && !disk.injected) break;
    if (disk.injected) ++faults;
    if (fault == -1) assert(result == cj::ReplayResult::AppliedUnretired);
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    cj::Record retained{};
    size_t retainedSize = 0;
    const auto retainedState = cj::inspectRecord(0, retained, retainedSize);
    if (retainedState != cj::InspectResult::Valid) {
      std::fprintf(stderr, "%s %s replay fault %d result %d left record state %d\n", exfat ? "exFAT" : "FAT",
                   failStop ? "fail-stop" : "transient", fault, static_cast<int>(result),
                   static_cast<int>(retainedState));
      const auto lookup = Storage.lookupPath(SLOT);
      auto direct = Storage.open(SLOT, O_RDONLY);
      std::fprintf(stderr, "journal strict lookup %d; direct open %d\n", static_cast<int>(lookup), !!direct);
      std::fprintf(stderr, "crosspoint lookup %d; stats directory lookup %d\n",
                   static_cast<int>(Storage.lookupPath("/.crosspoint")),
                   static_cast<int>(Storage.lookupPath("/.crosspoint/reading_stats")));
      if (direct) {
        cj::Record raw{};
        const int read = direct.read(raw.data(), raw.size());
        std::fprintf(stderr, "direct bytes %d; expected prefix match %d\n", read,
                     read == static_cast<int>(size) && raw == record);
        direct.close();
      }
      assert(false);
    }
    assert(retainedSize == size && retained == record);
    assert(cj::inspectSeal(0, record, size) == cj::SealResult::Matching);
    auto fixture = Storage.open(FIXTURE, O_RDONLY);
    assert(fixture);
    uint8_t bytes[4]{};
    assert(fixture.read(bytes, sizeof(bytes)) == sizeof(bytes));
    assert(fixture.close());
    assert(bytes[0] == 12 && bytes[1] == 34 && bytes[2] == 56 && bytes[3] == 78);
    const auto anyCopy = [](const char* primary, const uint8_t* oldBytes, const uint8_t* newBytes, size_t length) {
      for (const char* suffix : {"", ".tmp", ".bak"}) {
        const auto path = std::string(primary) + suffix;
        auto file = Storage.open(path.c_str(), O_RDONLY);
        if (!file) continue;
        std::array<uint8_t, cj::GLOBAL_BYTES> bytes{};
        const bool read = file.fileSize() == length && file.read(bytes.data(), length) == static_cast<int>(length);
        const bool closed = file.close();
        if (read && closed &&
            (std::memcmp(bytes.data(), oldBytes, length) == 0 || std::memcmp(bytes.data(), newBytes, length) == 0))
          return true;
      }
      return false;
    };
    assert(anyCopy(bookPath, previous.book.data(), book.data(), book.size()));
    assert(anyCopy(globalPath, previous.global.data(), global.data(), global.size()));
    if (result == cj::ReplayResult::AppliedUnretired) {
      const auto exact = [](const char* path, const uint8_t* expected, size_t length) {
        auto file = Storage.open(path, O_RDONLY);
        std::array<uint8_t, cj::GLOBAL_BYTES> bytes{};
        return file && file.fileSize() == length && file.read(bytes.data(), length) == static_cast<int>(length) &&
               file.close() && std::memcmp(bytes.data(), expected, length) == 0;
      };
      assert(exact(bookPath, book.data(), book.size()));
      assert(exact(globalPath, global.data(), global.size()));
    }
    // Force later allocations before checking again: readable stale bytes can
    // otherwise conceal a cluster that was freed by an interrupted rename.
    auto later = Storage.open("/later-allocation.bin", O_CREAT | O_EXCL | O_WRONLY);
    assert(later);
    std::array<uint8_t, 1024> fill{};
    fill.fill(0xa6);
    for (unsigned i = 0; i < 32; ++i) assert(later.write(fill.data(), fill.size()) == fill.size());
    assert(later.sync() && later.close());
    assert(cj::inspectRecord(0, retained, retainedSize) == cj::InspectResult::Valid);
    assert(retainedSize == size && retained == record);
    assert(cj::inspectSeal(0, record, size) == cj::SealResult::Matching);
    assert(anyCopy(bookPath, previous.book.data(), book.data(), book.size()));
    assert(anyCopy(globalPath, previous.global.data(), global.data(), global.size()));
    fixture = Storage.open(FIXTURE, O_RDONLY);
    assert(fixture && fixture.read(bytes, sizeof(bytes)) == sizeof(bytes) && fixture.close());
    assert(bytes[0] == 12 && bytes[1] == 34 && bytes[2] == 56 && bytes[3] == 78);
  }
  assert(faults > 0);
  Disk appliedBaseline;
  appliedBaseline.sectors = baseline.sectors;
  {
    FsVolume volume;
    assert(volume.begin(&appliedBaseline));
    Storage.attach(volume);
    assert(cj::replay() == cj::ReplayResult::AppliedUnretired);
  }
  int retirementFaults = 0;
  for (int fault = -1; fault < 80; ++fault) {
    Disk disk;
    disk.sectors = appliedBaseline.sectors;
    cj::RetirementResult result;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      disk.failAt = fault;
      disk.failStop = failStop;
      result = cj::retire();
    }
    if (fault >= 0 && !disk.injected) break;
    if (disk.injected) ++retirementFaults;
    if (fault == -1) assert(result == cj::RetirementResult::Retired);
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    cj::Record retained{};
    size_t retainedSize = 0;
    assert(cj::inspectRecord(0, retained, retainedSize) == cj::InspectResult::Valid);
    assert(retainedSize == size && retained == record);
    assert(cj::inspectSeal(0, record, size) == cj::SealResult::Matching);
    cj::Record candidate{};
    size_t candidateSize = 0;
    unsigned candidateSlot = 99;
    const auto state = cj::scanRecords(candidate, candidateSize, candidateSlot);
    if (result == cj::RetirementResult::Retired) assert(state == cj::ScanResult::Empty);
    assert(state == cj::ScanResult::Empty || state == cj::ScanResult::Candidate || state == cj::ScanResult::Damaged ||
           state == cj::ScanResult::Unavailable);
    auto fixture = Storage.open(FIXTURE, O_RDONLY);
    assert(fixture);
    uint8_t bytes[4]{};
    assert(fixture.read(bytes, sizeof(bytes)) == sizeof(bytes) && fixture.close());
    assert(bytes[0] == 12 && bytes[1] == 34 && bytes[2] == 56 && bytes[3] == 78);
  }
  assert(retirementFaults > 0);
  std::printf("%s %s: %d replay write faults retained journal and snapshot copies\n", exfat ? "exFAT" : "FAT",
              failStop ? "fail-stop" : "transient", faults);
  std::printf("%s %s: %d start write faults preserved canonical stats\n", exfat ? "exFAT" : "FAT",
              failStop ? "fail-stop" : "transient", startFaults);
  std::printf("%s %s: %d retirement write faults retained journal\n", exfat ? "exFAT" : "FAT",
              failStop ? "fail-stop" : "transient", retirementFaults);
}
#if defined(SDFAT_ALIAS_EXPERIMENT)
#if defined(SDFAT_ALIAS_ONE_SECTOR)
void experimentalTornAliasWrites(const Disk& baseline, const char* source, const char* target,
                                 const readingstats::completionmove::FileIdentity& identity) {
  namespace cm = readingstats::completionmove;
  int errors = 0, trials = 0, changedTrials = 0, hiddenAliases = 0;
  for (int bytes = 0; bytes < 512; bytes += 16) {
    Disk disk;
    disk.sectors = baseline.sectors;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      auto root = Storage.openBatchRoot();
      FsFile duplicate, survivor;
      assert(root && duplicate.open(&root, source + 1, O_RDWR) && survivor.open(&root, target + 1, O_RDONLY));
      disk.failAt = 0;
      disk.failStop = true;
      disk.tornAfter = bytes;
      duplicate.experimentUnlinkAlias(survivor);
      duplicate.close();
      survivor.close();
      root.close();
    }
    if (!disk.injected) continue;
    ++trials;
    bool changed = false;
    for (const auto& [number, sector] : disk.sectors) {
      const auto it = baseline.sectors.find(number);
      if (it == baseline.sectors.end() || it->second != sector) {
        changed = true;
        break;
      }
    }
    changedTrials += changed ? 1 : 0;
    disk.dead = false;
    disk.failAt = -1;
    disk.tornAfter = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    cm::FileIdentity after;
    assert(cm::captureFile(target, after) == cm::FileResult::Ready);
    assert(after.size == identity.size && after.checksum == identity.checksum);
    const auto sourceState = Storage.lookupPath(source);
    if (sourceState == HalStorage::PathStatus::Failed) ++errors;
    auto root = Storage.openBatchRoot();
    FsFile survivor, directory;
    assert(root && survivor.open(&root, target + 1, O_RDONLY) && directory.open(&root, "books", O_RDONLY));
    const auto cluster = survivor.firstCluster();
    FsFile child;
    while (child.openNext(&directory, O_RDONLY)) {
      if (child.isFile() && cluster && child.firstCluster() == cluster &&
          sourceState == HalStorage::PathStatus::Missing)
        ++hiddenAliases;
      assert(child.close());
    }
    assert(survivor.close() && directory.close() && root.close());
  }
  assert(trials > 0 && changedTrials > 0);
  std::printf("FAT: %d torn sector writes tested (%d changed disk), %d unreadable lookups, %d hidden aliases\n", trials,
              changedTrials, errors, hiddenAliases);
}
#endif

void experimentalAliasFaults(const Disk& baseline, bool exfat, bool failStop, const char* source, const char* target,
                             const readingstats::completionmove::FileIdentity& identity,
                             const readingstats::completionmove::FileIdentity* blocker = nullptr) {
  namespace cm = readingstats::completionmove;
  int failures = 0, retried = 0, unresolved = 0;
  for (int fault = -1; fault < 40; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    bool result = false;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      auto root = Storage.openBatchRoot();
      FsFile duplicate, survivor;
      assert(root && duplicate.open(&root, source + 1, O_RDWR) && survivor.open(&root, target + 1, O_RDONLY));
      assert(!duplicate.experimentUnlinkAlias(duplicate));
      if (fault == -1) {
        assert(cm::copyFileChecked(source, "/independent.epub", identity) == cm::FileResult::Copied);
        FsFile independent;
        assert(independent.open(&root, "independent.epub", O_RDONLY));
        assert(!duplicate.experimentUnlinkAlias(independent));
        assert(independent.close());
      }
      disk.failAt = fault;
      disk.failStop = failStop;
      result = duplicate.experimentUnlinkAlias(survivor);
      duplicate.close();
      survivor.close();
      root.close();
    }
    if (fault >= 0 && !disk.injected) break;
    if (disk.injected) ++failures;
    if (fault == -1) assert(result);
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    auto allocated = Storage.open("/alias-after.bin", O_CREAT | O_EXCL | O_WRONLY);
    assert(allocated);
    std::array<uint8_t, 512> fill{};
    fill.fill(0xca);
    for (int n = 0; n < 64; ++n) assert(allocated.write(fill.data(), fill.size()) == fill.size());
    assert(allocated.sync() && allocated.close());
    cm::FileIdentity survived;
    assert(cm::captureFile(target, survived) == cm::FileResult::Ready);
    assert(survived.size == identity.size && survived.checksum == identity.checksum);
    const auto sourceState = Storage.lookupPath(source);
    const int unresolvedBefore = unresolved;
    if (sourceState == HalStorage::PathStatus::Present) {
      auto root = Storage.openBatchRoot();
      FsFile duplicate, survivor;
      if (root && duplicate.open(&root, source + 1, O_RDWR) && survivor.open(&root, target + 1, O_RDONLY) &&
          duplicate.experimentUnlinkAlias(survivor))
        ++retried;
      else
        ++unresolved;
      if (duplicate) duplicate.close();
      if (survivor) survivor.close();
      if (root) root.close();
    } else if (sourceState == HalStorage::PathStatus::Missing) {
      ++retried;
    } else {
      ++unresolved;
    }
    if (unresolved != unresolvedBefore)
      std::printf("  alias retry unresolved: fault=%d unlink_result=%d source_status=%d\n", fault, result,
                  static_cast<int>(sourceState));
    cm::FileIdentity afterRetry;
    assert(cm::captureFile(target, afterRetry) == cm::FileResult::Ready);
    assert(afterRetry.size == identity.size && afterRetry.checksum == identity.checksum);
    if (blocker) {
      cm::FileIdentity afterBlocker;
      assert(cm::captureFile("/blocker.bin", afterBlocker) == cm::FileResult::Ready);
      assert(afterBlocker.size == blocker->size && afterBlocker.checksum == blocker->checksum);
    }
    auto progress = Storage.open(FIXTURE, O_RDONLY);
    assert(progress);
    uint8_t marker[4]{};
    assert(progress.read(marker, sizeof(marker)) == sizeof(marker) && progress.close());
    assert(marker[0] == 12 && marker[1] == 34 && marker[2] == 56 && marker[3] == 78);
  }
  assert(failures > 0);
  std::printf("%s %s: experimental metadata-only unlink retained data under %d failures\n", exfat ? "exFAT" : "FAT",
              failStop ? "fail-stop" : "transient", failures);
  std::printf("%s %s: experimental alias cleanup %d resolved, %d unresolved\n", exfat ? "exFAT" : "FAT",
              failStop ? "fail-stop" : "transient", retried, unresolved);
}

void experimentalAliasReadFaults(const Disk& baseline, bool exfat, const char* source, const char* target,
                                 const readingstats::completionmove::FileIdentity& identity,
                                 const readingstats::completionmove::FileIdentity* blocker = nullptr) {
  namespace cm = readingstats::completionmove;
  int failures = 0;
  for (int fault = 0; fault < 80; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      auto root = Storage.openBatchRoot();
      FsFile duplicate, survivor;
      assert(root && duplicate.open(&root, source + 1, O_RDWR) && survivor.open(&root, target + 1, O_RDONLY));
      disk.failReadAt = disk.reads + fault;
      const bool result = duplicate.experimentUnlinkAlias(survivor);
      if (disk.injectedRead) assert(!result);
      duplicate.close();
      survivor.close();
      root.close();
    }
    if (!disk.injectedRead) break;
    ++failures;
    disk.failReadAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    auto allocated = Storage.open("/alias-after.bin", O_CREAT | O_EXCL | O_WRONLY);
    assert(allocated);
    std::array<uint8_t, 512> fill{};
    fill.fill(0xc5);
    for (int n = 0; n < 64; ++n) assert(allocated.write(fill.data(), fill.size()) == fill.size());
    assert(allocated.sync() && allocated.close());
    cm::FileIdentity survived;
    assert(cm::captureFile(target, survived) == cm::FileResult::Ready);
    assert(survived.size == identity.size && survived.checksum == identity.checksum);
    if (blocker) {
      cm::FileIdentity afterBlocker;
      assert(cm::captureFile("/blocker.bin", afterBlocker) == cm::FileResult::Ready);
      assert(afterBlocker.size == blocker->size && afterBlocker.checksum == blocker->checksum);
    }
    auto progress = Storage.open(FIXTURE, O_RDONLY);
    assert(progress);
    uint8_t marker[4]{};
    assert(progress.read(marker, sizeof(marker)) == sizeof(marker) && progress.close());
    assert(marker[0] == 12 && marker[1] == 34 && marker[2] == 56 && marker[3] == 78);
  }
  std::printf("%s: experimental alias unlink retained data under %d read failures\n", exfat ? "exFAT" : "FAT",
              failures);
}

#if defined(SDFAT_ALIAS_LARGE)
#if defined(SDFAT_ALIAS_ONE_SECTOR)
constexpr char LOCATOR_PATH[] = "/alias-locator.bin";
constexpr char LOCATOR_SEAL[] = "/alias-seal.bin";
constexpr aliasrecord::VolumeToken TEST_VOLUME = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

void createFixtureCompletion(FsVolume& volume, const std::string& source, const std::string& target) {
  const auto directory = "/.crosspoint/reading_stats/epub_" + std::to_string(std::hash<std::string>{}(source));
  assert(volume.mkdir(directory.c_str(), true) && volume.mkdir("/.completion-journal", true));
  cj::Baseline previous{};
  previous.book[0] = 5;
  previous.global[0] = 3;
  previous.bookSize = cj::BOOK_BYTES;
  previous.globalSize = cj::GLOBAL_BYTES;
  const auto write = [](const std::string& path, const uint8_t* data, size_t size) {
    auto file = Storage.open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY);
    assert(file && file.write(data, size) == size && file.sync() && file.close());
  };
  write(directory + "/stats_v5.bin", previous.book.data(), previous.bookSize);
  write("/.crosspoint/global_stats.bin", previous.global.data(), previous.globalSize);
  auto book = previous.book;
  auto global = previous.global;
  book[11] = 1;
  global[13] = 1;
  cj::Record record{};
  size_t size = 0;
  assert(cj::encode(directory, book, global, previous, record, size, cj::MOVE_TO_READ));
  assert(cj::start(record, size) == cj::StartResult::Started);
  assert(cj::replay() == cj::ReplayResult::AppliedUnretired);
  assert(readingstats::completionmove::preparePlan(source, target) == readingstats::completionmove::PlanResult::Ready);
}

bool currentTransaction(uint64_t& transaction) {
  cj::Record record{};
  size_t size = 0;
  unsigned slot = 0;
  if (cj::scanRecords(record, size, slot) != cj::ScanResult::Candidate || !(record[9] & cj::MOVE_TO_READ)) return false;
  readingstats::completionmove::Plan plan{};
  size_t planSize = 0;
  std::string_view from, to;
  if (readingstats::completionmove::inspectPlan(plan, planSize, from, to) !=
      readingstats::completionmove::PlanResult::Ready)
    return false;
  // Bind this prototype locator to the actual completion record, sealed move
  // plan, slot and EPUB item (index zero), rather than a hard-coded ID.
  uint8_t material[18]{};
  size_t offset = 0;
  aliasrecord::put(material, offset, cj::checksum(record.data(), size), 8);
  aliasrecord::put(material, offset, cj::checksum(plan.data(), planSize), 8);
  aliasrecord::put(material, offset, slot, 1);
  aliasrecord::put(material, offset, 0, 1);
  transaction = cj::checksum(material, sizeof(material));
  return true;
}

bool writeLocator(const FsFile::ExperimentLocator& locator) {
  uint64_t transaction = 0;
  if (!currentTransaction(transaction)) return false;
  aliasrecord::Record record{};
  if (!aliasrecord::encode(locator, TEST_VOLUME, transaction, record)) return false;
  auto file = Storage.open(LOCATOR_PATH, O_CREAT | O_EXCL | O_WRONLY);
  if (!file) return false;
  const bool written = file.write(record.data(), record.size()) == record.size();
  const bool synced = file.sync(), closed = file.close();
  if (!written || !synced || !closed) return false;
  auto check = Storage.open(LOCATOR_PATH, O_RDONLY);
  aliasrecord::Record observed{};
  if (!check || check.read(observed.data(), observed.size()) != observed.size() || !check.close() || observed != record)
    return false;
  const auto seal = aliasrecord::seal(record, transaction);
  auto marker = Storage.open(LOCATOR_SEAL, O_CREAT | O_EXCL | O_WRONLY);
  if (!marker) return false;
  const bool markerWritten = marker.write(seal.data(), seal.size()) == seal.size();
  const bool markerSynced = marker.sync(), markerClosed = marker.close();
  return markerWritten && markerSynced && markerClosed;
}

bool loadLocator(FsFile::ExperimentLocator& output, const aliasrecord::VolumeToken& volume = TEST_VOLUME,
                 uint64_t expectedTransaction = 0) {
  uint64_t transaction = 0;
  if (!currentTransaction(transaction) || (expectedTransaction && expectedTransaction != transaction)) return false;
  aliasrecord::Record record{};
  aliasrecord::Seal actual{};
  auto file = Storage.open(LOCATOR_PATH, O_RDONLY);
  if (!file) return false;
  const bool size = file.fileSize() == record.size();
  const bool read = size && file.read(record.data(), record.size()) == record.size();
  const bool closed = file.close();
  if (!read || !closed) return false;
  auto marker = Storage.open(LOCATOR_SEAL, O_RDONLY);
  if (!marker) return false;
  const bool sealSize = marker.fileSize() == actual.size();
  const bool sealRead = sealSize && marker.read(actual.data(), actual.size()) == actual.size();
  const bool sealClosed = marker.close();
  return sealRead && sealClosed && actual == aliasrecord::seal(record, transaction) &&
         aliasrecord::decode(record, volume, transaction, output);
}

bool sameLocator(const FsFile::ExperimentLocator& a, const FsFile::ExperimentLocator& b) {
  return a.parentCluster == b.parentCluster && a.firstCluster == b.firstCluster && a.fileSize == b.fileSize &&
         a.directoryIndex == b.directoryIndex && a.lfnCount == b.lfnCount &&
         std::memcmp(a.entries, b.entries, sizeof(a.entries)) == 0;
}

void experimentalLocatorPublishFaults(const Disk& baseline, bool failStop, const char* target,
                                      const readingstats::completionmove::FileIdentity& identity,
                                      const FsFile::ExperimentLocator& locator) {
  namespace cm = readingstats::completionmove;
  int injected = 0, valid = 0;
  for (int fault = -1; fault < 40; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    bool result = false;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      disk.failAt = fault;
      disk.failStop = failStop;
      result = writeLocator(locator);
    }
    if (fault >= 0 && !disk.injected) break;
    if (disk.injected) ++injected;
    if (fault == -1) assert(result);
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    FsFile::ExperimentLocator loaded{};
    if (loadLocator(loaded)) {
      assert(sameLocator(loaded, locator));
      ++valid;
    }
    auto allocation = Storage.open("/publish-after.bin", O_CREAT | O_EXCL | O_WRONLY);
    assert(allocation);
    std::array<uint8_t, 512> fill{};
    fill.fill(0xb6);
    for (int n = 0; n < 64; ++n) assert(allocation.write(fill.data(), fill.size()) == fill.size());
    assert(allocation.sync() && allocation.close());
    cm::FileIdentity after;
    assert(cm::captureFile(target, after) == cm::FileResult::Ready && after.size == identity.size &&
           after.checksum == identity.checksum);
  }
  assert(injected > 0 && valid > 0);
  std::printf("FAT %s: locator publication survived %d injected write faults; %d sealed records readable\n",
              failStop ? "fail-stop" : "transient", injected, valid);
  int tornTrials = 0, readable = 0;
  for (int fault = 0; fault < 20; ++fault) {
    bool any = false;
    for (const int bytes : {0, 1, 16, 32, 64, 128, 256, 511}) {
      Disk disk;
      disk.sectors = baseline.sectors;
      {
        FsVolume volume;
        assert(volume.begin(&disk));
        Storage.attach(volume);
        disk.failAt = fault;
        disk.failStop = true;
        disk.tornAfter = bytes;
        writeLocator(locator);
      }
      if (!disk.injected) continue;
      any = true;
      ++tornTrials;
      disk.dead = false;
      disk.failAt = -1;
      disk.tornAfter = -1;
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      FsFile::ExperimentLocator loaded{};
      if (loadLocator(loaded)) {
        assert(sameLocator(loaded, locator));
        ++readable;
      }
      cm::FileIdentity after;
      assert(cm::captureFile(target, after) == cm::FileResult::Ready && after.size == identity.size &&
             after.checksum == identity.checksum);
    }
    if (!any) break;
  }
  assert(tornTrials > 0);
  std::printf("FAT %s: %d torn locator publication trials; %d sealed records readable\n",
              failStop ? "fail-stop" : "transient", tornTrials, readable);
}

void experimentalLocatorFaults(const Disk& baseline, bool failStop, const char* source, const char* target,
                               const readingstats::completionmove::FileIdentity& identity,
                               const FsFile::ExperimentLocator& locator) {
  namespace cm = readingstats::completionmove;
  experimentalLocatorPublishFaults(baseline, failStop, target, identity, locator);
  Disk prepared;
  prepared.sectors = baseline.sectors;
  {
    FsVolume volume;
    assert(volume.begin(&prepared));
    Storage.attach(volume);
    assert(writeLocator(locator));
    uint64_t transaction = 0;
    assert(currentTransaction(transaction));
    FsFile::ExperimentLocator decoded{};
    assert(loadLocator(decoded));
    auto wrongVolume = TEST_VOLUME;
    wrongVolume[0] ^= 1;
    assert(!loadLocator(decoded, wrongVolume));
    assert(!loadLocator(decoded, TEST_VOLUME, transaction + 1));
    aliasrecord::Record record{};
    assert(aliasrecord::encode(locator, TEST_VOLUME, transaction, record));
    for (size_t i = 0; i < record.size(); ++i) {
      auto changed = record;
      changed[i] ^= 1;
      assert(!aliasrecord::decode(changed, TEST_VOLUME, transaction, decoded));
    }
  }
  {
    // Model another card with copied sectors but a different supplied token.
    Disk swapped;
    swapped.sectors = prepared.sectors;
    FsVolume volume;
    assert(volume.begin(&swapped));
    Storage.attach(volume);
    auto otherToken = TEST_VOLUME;
    otherToken[8] ^= 0x80;
    FsFile::ExperimentLocator rejected{};
    assert(!loadLocator(rejected, otherToken));
    assert(loadLocator(rejected, TEST_VOLUME));
  }
  {
    // A different, valid and sealed completion must not adopt an old locator.
    Disk changed;
    changed.sectors = prepared.sectors;
    FsVolume volume;
    assert(volume.begin(&changed));
    Storage.attach(volume);
    cj::Record old{};
    size_t oldSize = 0;
    unsigned oldSlot = 0;
    assert(cj::scanRecords(old, oldSize, oldSlot) == cj::ScanResult::Candidate);
    std::string_view directory;
    const uint8_t *oldBook = nullptr, *oldGlobal = nullptr;
    assert(cj::decode(old.data(), oldSize, directory, oldBook, oldGlobal));
    cj::Baseline previous{};
    std::copy_n(oldBook, previous.book.size(), previous.book.begin());
    std::copy_n(oldGlobal, previous.global.size(), previous.global.begin());
    previous.bookSize = previous.book.size();
    previous.globalSize = previous.global.size();
    auto nextBook = previous.book;
    auto nextGlobal = previous.global;
    nextBook[11] = 0;
    nextGlobal[13] = 0;
    cj::Record replacement{};
    size_t replacementSize = 0;
    assert(cj::encode(directory, nextBook, nextGlobal, previous, replacement, replacementSize, cj::MOVE_TO_READ));
    assert(cj::retire() == cj::RetirementResult::Retired);
    assert(cj::start(replacement, replacementSize) == cj::StartResult::Started);
    FsFile::ExperimentLocator rejected{};
    assert(!loadLocator(rejected));
  }
  {
    // A damaged move plan must not authorize the original locator either.
    Disk changed;
    changed.sectors = prepared.sectors;
    FsVolume volume;
    assert(volume.begin(&changed));
    Storage.attach(volume);
    auto plan = Storage.open("/.completion-journal/.cmove-00", O_RDWR);
    assert(plan);
    uint8_t byte = 0;
    assert(plan.read(&byte, 1) == 1 && plan.seekSet(0));
    byte ^= 0x40;
    assert(plan.write(&byte, 1) == 1 && plan.sync() && plan.close());
    FsFile::ExperimentLocator rejected{};
    assert(!loadLocator(rejected));
  }
  int failures = 0;
  for (int fault = -1; fault < 40; ++fault) {
    Disk disk;
    disk.sectors = prepared.sectors;
    bool result = false;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      FsFile::ExperimentLocator stored{};
      assert(loadLocator(stored));
      auto root = Storage.openBatchRoot();
      FsFile parent, survivor;
      assert(root && parent.open(&root, "books", O_RDONLY) && survivor.open(&root, target + 1, O_RDONLY));
      auto wrong = stored;
      wrong.entries[0][1] ^= 1;
      assert(!parent.experimentRecoverLocator(survivor, wrong));
      disk.failAt = fault;
      disk.failStop = failStop;
      result = parent.experimentRecoverLocator(survivor, stored);
      parent.close();
      survivor.close();
      root.close();
    }
    if (fault >= 0 && !disk.injected) break;
    if (disk.injected) ++failures;
    if (fault == -1) assert(result);
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    FsFile::ExperimentLocator stored{};
    assert(loadLocator(stored));
    auto root = Storage.openBatchRoot();
    FsFile parent, survivor;
    assert(root && parent.open(&root, "books", O_RDONLY) && survivor.open(&root, target + 1, O_RDONLY));
    assert(parent.experimentRecoverLocator(survivor, stored));
    assert(parent.close() && survivor.close() && root.close());
    assert(Storage.lookupPath(source) == HalStorage::PathStatus::Missing);
    auto allocation = Storage.open("/locator-after.bin", O_CREAT | O_EXCL | O_WRONLY);
    assert(allocation);
    std::array<uint8_t, 512> fill{};
    fill.fill(0xd3);
    for (int n = 0; n < 64; ++n) assert(allocation.write(fill.data(), fill.size()) == fill.size());
    assert(allocation.sync() && allocation.close());
    cm::FileIdentity after;
    assert(cm::captureFile(target, after) == cm::FileResult::Ready && after.size == identity.size &&
           after.checksum == identity.checksum);
    if (fault == -1) {
      auto replacement = Storage.open("/books/OTHER.TXT", O_CREAT | O_EXCL | O_WRONLY);
      assert(replacement);
      const uint8_t other[] = {9, 8, 7};
      assert(replacement.write(other, sizeof(other)) == sizeof(other));
      assert(replacement.sync() && replacement.close());
      auto checkRoot = Storage.openBatchRoot();
      FsFile checkParent, checkSurvivor;
      assert(checkRoot && checkParent.open(&checkRoot, "books", O_RDONLY) &&
             checkSurvivor.open(&checkRoot, target + 1, O_RDONLY));
      assert(!checkParent.experimentRecoverLocator(checkSurvivor, stored));
      assert(checkParent.close() && checkSurvivor.close() && checkRoot.close());
      auto verify = Storage.open("/books/OTHER.TXT", O_RDONLY);
      assert(verify);
      uint8_t value[sizeof(other)]{};
      assert(verify.read(value, sizeof(value)) == sizeof(value) && verify.close());
      assert(std::memcmp(value, other, sizeof(other)) == 0);
    }
    auto progress = Storage.open(FIXTURE, O_RDONLY);
    assert(progress);
    uint8_t marker[4]{};
    assert(progress.read(marker, sizeof(marker)) == sizeof(marker) && progress.close());
    assert(marker[0] == 12 && marker[1] == 34 && marker[2] == 56 && marker[3] == 78);
  }
  assert(failures > 0);
  std::printf("FAT %s: cross-sector locator recovery retried %d write faults\n", failStop ? "fail-stop" : "transient",
              failures);
  int tornTrials = 0;
  for (int write = 0; write < 2; ++write)
    for (int bytes = 0; bytes < 512; bytes += 16) {
      Disk disk;
      disk.sectors = prepared.sectors;
      {
        FsVolume volume;
        assert(volume.begin(&disk));
        Storage.attach(volume);
        FsFile::ExperimentLocator stored{};
        assert(loadLocator(stored));
        auto root = Storage.openBatchRoot();
        FsFile parent, survivor;
        assert(root && parent.open(&root, "books", O_RDONLY) && survivor.open(&root, target + 1, O_RDONLY));
        disk.failAt = write;
        disk.failStop = true;
        disk.tornAfter = bytes;
        parent.experimentRecoverLocator(survivor, stored);
        parent.close();
        survivor.close();
        root.close();
      }
      if (!disk.injected) continue;
      ++tornTrials;
      disk.dead = false;
      disk.failAt = -1;
      disk.tornAfter = -1;
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      FsFile::ExperimentLocator stored{};
      assert(loadLocator(stored));
      auto root = Storage.openBatchRoot();
      FsFile parent, survivor;
      assert(root && parent.open(&root, "books", O_RDONLY) && survivor.open(&root, target + 1, O_RDONLY));
      assert(parent.experimentRecoverLocator(survivor, stored));
      assert(parent.experimentRecoverLocator(survivor, stored));
      assert(parent.close() && survivor.close() && root.close());
      assert(Storage.lookupPath(source) == HalStorage::PathStatus::Missing);
      cm::FileIdentity after;
      assert(cm::captureFile(target, after) == cm::FileResult::Ready && after.size == identity.size &&
             after.checksum == identity.checksum);
    }
  assert(tornTrials > 0);
  std::printf("FAT %s: cross-sector locator recovered %d torn-sector trials\n", failStop ? "fail-stop" : "transient",
              tornTrials);
}
#endif

void largeAliasFixture(bool exfat, bool failStop, bool fragmented, bool crossSector = false) {
  namespace cm = readingstats::completionmove;
  Disk baseline;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  } else {
    FatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  }
  constexpr char source[] = "/books/large.epub", target[] = "/read/large.epub";
  cm::FileIdentity identity, blockerIdentity;
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    assert(volume.mkdir("/books", true) && volume.mkdir("/read", true) && volume.mkdir("/.crosspoint", true));
    if (crossSector) {
      for (int i = 0; i < 13; ++i) {
        char path[48];
        std::snprintf(path, sizeof(path), "/books/PAD%02d.TXT", i);
        auto pad = Storage.open(path, O_CREAT | O_EXCL | O_WRONLY);
        assert(pad && pad.close());
      }
    }
    auto book = Storage.open(source, O_CREAT | O_EXCL | O_WRONLY);
    assert(book);
    HalFile blocker;
    if (fragmented) {
      blocker = Storage.open("/blocker.bin", O_CREAT | O_EXCL | O_WRONLY);
      assert(blocker);
    }
    std::array<uint8_t, 512> block{};
    for (size_t i = 0; i < block.size(); ++i) block[i] = static_cast<uint8_t>((i * 43) & 0xff);
    for (int i = 0; i < 512; ++i) {
      assert(book.write(block.data(), block.size()) == block.size());
      if (fragmented && i % 8 == 7)
        for (int j = 0; j < 8; ++j) assert(blocker.write(block.data(), block.size()) == block.size());
    }
    assert(book.sync() && book.close());
    if (fragmented) {
      assert(blocker.sync() && blocker.close());
      assert(cm::captureFile("/blocker.bin", blockerIdentity) == cm::FileResult::Ready);
    }
    auto progress = Storage.open(FIXTURE, O_CREAT | O_EXCL | O_WRONLY);
    assert(progress);
    const uint8_t marker[] = {12, 34, 56, 78};
    assert(progress.write(marker, sizeof(marker)) == sizeof(marker));
    assert(progress.sync() && progress.close());
    assert(cm::captureFile(source, identity) == cm::FileResult::Ready && identity.size == 256 * 1024);
#if defined(SDFAT_ALIAS_ONE_SECTOR)
    if (crossSector) createFixtureCompletion(volume, source, target);
#endif
#if defined(SDFAT_ALIAS_ONE_SECTOR)
    if (crossSector) {
      auto root = Storage.openBatchRoot();
      FsFile file;
      assert(root && file.open(&root, source + 1, O_RDONLY));
      assert(file.experimentCrossesSector());
      assert(file.close() && root.close());
    }
#endif
    if (fragmented && exfat) {
      auto root = Storage.openBatchRoot();
      FsFile file;
      assert(root && file.open(&root, source + 1, O_RDONLY));
      assert(!file.isContiguous());
      assert(file.close() && root.close());
    }
  }
  bool found = false;
  for (int fault = 0; fault < 30 && !found; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      disk.failAt = fault;
      disk.failStop = failStop;
      cm::moveFileChecked(source, target, identity);
    }
    if (!disk.injected) break;
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    cm::FileIdentity a, b;
    if (cm::captureFile(source, a) != cm::FileResult::Ready || cm::captureFile(target, b) != cm::FileResult::Ready ||
        a.size != identity.size || b.size != identity.size || a.checksum != identity.checksum ||
        b.checksum != identity.checksum)
      continue;
    auto root = Storage.openBatchRoot();
    FsFile left, right;
    assert(root && left.open(&root, source + 1, O_RDONLY) && right.open(&root, target + 1, O_RDONLY));
    const bool alias = left.firstCluster() && left.firstCluster() == right.firstCluster();
    assert(left.close() && right.close() && root.close());
    if (!alias) continue;
    found = true;
#if defined(SDFAT_ALIAS_ONE_SECTOR)
    if (crossSector) {
      auto dir = Storage.openBatchRoot();
      FsFile duplicate, survivor;
      assert(dir && duplicate.open(&dir, source + 1, O_RDWR) && survivor.open(&dir, target + 1, O_RDONLY));
      assert(duplicate.experimentCrossesSector());
      FsFile::ExperimentLocator locator{};
      assert(duplicate.experimentCaptureLocator(survivor, locator));
      assert(!duplicate.experimentUnlinkAlias(survivor));
      assert(duplicate.close() && survivor.close() && dir.close());
      cm::FileIdentity a2, b2;
      assert(cm::captureFile(source, a2) == cm::FileResult::Ready && a2.checksum == identity.checksum);
      assert(cm::captureFile(target, b2) == cm::FileResult::Ready && b2.checksum == identity.checksum);
      std::printf("FAT %s: cross-sector filename safely refused\n", failStop ? "fail-stop" : "transient");
      experimentalLocatorFaults(disk, failStop, source, target, identity, locator);
      Storage.attach(volume);
      continue;
    }
#endif
    experimentalAliasFaults(disk, exfat, failStop, source, target, identity, fragmented ? &blockerIdentity : nullptr);
    Storage.attach(volume);
    experimentalAliasReadFaults(disk, exfat, source, target, identity, fragmented ? &blockerIdentity : nullptr);
    std::printf("%s %s: %s 256 KiB alias fixture tested\n", exfat ? "exFAT" : "FAT",
                failStop ? "fail-stop" : "transient", fragmented ? "fragmented" : "contiguous");
  }
  assert(found);
}
#endif
#endif

void exfatFailedSyncRetainsMetadata(bool readFault) {
  Disk baseline;
  uint8_t scratch[512];
  ExFatFormatter formatter;
  assert(formatter.format(&baseline, scratch));
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    assert(volume.mkdir("/books", true) && volume.mkdir("/.crosspoint", true));
    auto progress = Storage.open(FIXTURE, O_CREAT | O_EXCL | O_WRONLY);
    const uint8_t state[] = {12, 34, 56, 78};
    assert(progress && progress.write(state, sizeof(state)) == sizeof(state) && progress.sync() && progress.close());
  }
  const auto path = "/books/" + std::string(180, 'q') + ".epub";
  int injected = 0;
  for (int fault = 0; fault < 32; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    bool failed = false;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      auto writer = Storage.open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY);
      const uint8_t data[] = {0x17, 0x39, 0x56};
      assert(writer && writer.write(data, sizeof(data)) == sizeof(data));
      if (readFault)
        disk.failReadAt = disk.reads + fault;
      else
        disk.failAt = disk.writes + fault;
      const bool syncOk = writer.sync();
      failed = disk.injected || disk.injectedRead;
      disk.failAt = -1;
      disk.failReadAt = -1;
      if (failed) assert(!syncOk);
      assert(writer.close());
    }
    if (!failed) break;
    ++injected;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      assert(Storage.lookupPath(path.c_str()) == HalStorage::PathStatus::Present);
      auto saved = Storage.open(path.c_str(), O_RDONLY);
      uint8_t bytes[3]{};
      assert(saved && saved.fileSize() == sizeof(bytes) && saved.read(bytes, sizeof(bytes)) == sizeof(bytes) &&
             saved.close());
      assert(bytes[0] == 0x17 && bytes[1] == 0x39 && bytes[2] == 0x56);
      auto progress = Storage.open(FIXTURE, O_RDONLY);
      uint8_t state[4]{};
      assert(progress && progress.read(state, sizeof(state)) == sizeof(state) && progress.close());
      assert(state[0] == 12 && state[1] == 34 && state[2] == 56 && state[3] == 78);
    }
  }
  assert(injected > 0);
  std::printf("exFAT: %d injected sync %s failures retained retried metadata and progress\n", injected,
              readFault ? "read" : "write");
}

void verifyMovedFilesOnRealFilesystem(bool exfat, bool removeRecent = false) {
  namespace cm = readingstats::completionmove;
  namespace dc = readingstats::deferredcompletion;
#if defined(SDFAT_REAL_MOVE_STORES)
  const auto progressFormat = removeRecent ? movedstate::ProgressFormat::TextOffset
                              : exfat      ? movedstate::ProgressFormat::Legacy
                                           : movedstate::ProgressFormat::PageCount;
#endif
  Disk disk;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter formatter;
    assert(formatter.format(&disk, scratch));
  } else {
    FatFormatter formatter;
    assert(formatter.format(&disk, scratch));
  }
  constexpr char source[] = "/books/verify.epub";
  constexpr char target[] = "/read/verify.epub";
  decltype(disk.sectors) beforeRetirement;
  decltype(disk.sectors) retiredBaseline;
  decltype(disk.sectors) beforeMove;
  uint64_t retiredHash = 0;
  {
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    const auto directory = "/.crosspoint/reading_stats/epub_" + std::to_string(std::hash<std::string>{}(source));
    assert(volume.mkdir("/books", true) && volume.mkdir("/read", true) && volume.mkdir(directory.c_str(), true) &&
           volume.mkdir("/.completion-journal", true) && volume.mkdir("/.completion-deferred", true));
    const auto write = [](const std::string& path, const uint8_t* data, size_t size) {
      auto file = Storage.open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY);
      assert(file && file.write(data, size) == size && file.sync() && file.close());
    };
    const uint8_t contents[] = {1, 3, 5, 7};
    write(source, contents, sizeof(contents));
#if defined(SDFAT_REAL_MOVE_STORES)
    movedstate::seedBookmarks(source, removeRecent ? 4 : 2);
    movedstate::checkBookmarks(source, removeRecent ? 4 : 2);
    movedstate::seedProgress(source, removeRecent, progressFormat);
#endif
    cj::Baseline previous{};
    previous.book[0] = 5;
    previous.global[0] = 3;
    previous.bookSize = cj::BOOK_BYTES;
    previous.globalSize = cj::GLOBAL_BYTES;
    write(directory + "/stats_v5.bin", previous.book.data(), previous.bookSize);
    write("/.crosspoint/global_stats.bin", previous.global.data(), previous.globalSize);
    auto book = previous.book;
    auto global = previous.global;
    book[11] = 1;
    global[13] = 1;
    cj::Record record{};
    size_t recordSize = 0;
    constexpr uint64_t origin = 0x12345678;
    assert(cj::encode(directory, book, global, previous, record, recordSize,
                      cj::MOVE_TO_READ | cj::MOVE_FROM_DEFERRED | (removeRecent ? cj::REMOVE_RECENTS : 0), source,
                      origin));
    dc::Record intent{};
    size_t intentSize = 0;
    dc::Seal seal{};
    assert(dc::encode(source, origin, intent, intentSize));
    assert(dc::encodeSeal(0, intent, intentSize, seal));
    write("/.completion-deferred/.defer-00", intent.data(), intentSize);
    write("/.completion-deferred/.dseal-00", seal.data(), seal.size());
#if defined(SDFAT_REAL_MOVE_STORES)
    {
      StatsSessionLease reader;
      assert(reader.acquire() && StatsSessionLease::anyActive());
      assert(readingstats::setClosedBookCompletionStats(source, true, {}, cj::MOVE_TO_READ) ==
             readingstats::ClosedCompletionResult::Busy);
      assert(Storage.lookupPath("/.completion-journal/.completion-00") == HalStorage::PathStatus::Missing);
      assert(Storage.lookupPath(source) == HalStorage::PathStatus::Present);
      assert(reader.release() && !StatsSessionLease::anyActive());
    }
#endif
    assert(cj::start(record, recordSize) == cj::StartResult::Started);
    assert(cj::replay() == cj::ReplayResult::AppliedUnretired);
#if defined(SDFAT_REAL_MOVE_STORES)
    {
      const auto primary = movedstate::bookmarkPath(source, true);
      const auto recovery = primary + (removeRecent ? ".bookmark-edit.guard" : ".bookmark-edit.bak");
      assert(Storage.rename(primary.c_str(), recovery.c_str()));
      movedstate::checkBookmarks(source, removeRecent ? 4 : 2, true);
      assert(cm::preparePlan(source, target) == cm::PlanResult::Conflict);
      assert(Storage.lookupPath(source) == HalStorage::PathStatus::Present &&
             Storage.lookupPath(target) == HalStorage::PathStatus::Missing &&
             Storage.lookupPath(recovery.c_str()) == HalStorage::PathStatus::Present);
      assert(Storage.rename(recovery.c_str(), primary.c_str()));
    }
#endif
    assert(cm::preparePlan(source, target) == cm::PlanResult::Ready);
    beforeMove = disk.sectors;
    const int before = disk.writes;
    assert(cm::verifyPreparedFiles() == cm::VerifyFilesResult::NotReady);
    assert(disk.writes == before);
    assert(cm::movePreparedFiles() == cm::MoveFilesResult::Moved);
    const int after = disk.writes;
    assert(cm::verifyPreparedFiles() == cm::VerifyFilesResult::ContentMatches);
    assert(disk.writes == after);
    beforeRetirement = disk.sectors;
    assert(dc::retireMovedIntent(source) == dc::RetireMoveResult::Retired);
    assert(dc::retireMovedIntent(source) == dc::RetireMoveResult::AlreadyRetired);
  }
  {
    Disk integrated;
    integrated.sectors = beforeMove;
    uint64_t moveHash = 0;
    {
      FsVolume volume;
      assert(volume.begin(&integrated));
      Storage.attach(volume);
      cj::Record journal{};
      size_t size = 0;
      unsigned slot = 99;
      assert(cj::scanRecords(journal, size, slot) == cj::ScanResult::Candidate);
      dc::Record request{};
      size_t requestSize = 0;
      unsigned requestSlot = 99;
      assert(dc::discoverResumable(request, requestSize, requestSlot) == dc::FindResult::Found);
      moveHash = cj::checksum(journal.data(), size);
      APP_STATE.openEpubPath = source;
#if defined(SDFAT_REAL_MOVE_STORES)
      JsonDocument recent;
      auto entry = recent["books"].to<JsonArray>().add<JsonObject>();
      entry["path"] = source;
      entry["title"] = "Book";
      entry["author"] = "Writer";
      entry["coverBmpPath"] = "/.crosspoint/old-cover.bmp";
      auto other = recent["books"].add<JsonObject>();
      other["path"] = "/other.epub";
      other["title"] = "Other";
      other["author"] = "Else";
      assert(RECENT_BOOKS.fromJson(recent.as<JsonVariantConst>()));
      APP_STATE.favoriteSleepImagePath = "/favorite.bmp";
      APP_STATE.readerActivityLoadCount = 7;
      for (const auto& pair :
           {std::pair{"/.crosspoint/state.json",
                      std::string("{\"openEpubPath\":\"") + source +
                          "\",\"favoriteSleepImagePath\":\"/favorite.bmp\",\"readerActivityLoadCount\":7}"},
            std::pair{
                "/.crosspoint/recent.json",
                std::string("{\"books\":[{\"path\":\"") + source +
                    "\",\"title\":\"Book\",\"author\":\"Writer\",\"coverBmpPath\":\"/.crosspoint/old-cover.bmp\"},"
                    "{\"path\":\"/other.epub\",\"title\":\"Other\",\"author\":\"Else\"}]}"}}) {
        auto file = Storage.open(pair.first, O_CREAT | O_EXCL | O_WRONLY);
        assert(file && file.write(pair.second.data(), pair.second.size()) == pair.second.size() && file.sync() &&
               file.close());
      }
      auto blocker = Storage.open("/.crosspoint/recent.json.tmp", O_CREAT | O_EXCL | O_WRONLY);
      assert(blocker && blocker.sync() && blocker.close());
#else
      RECENT_BOOKS.books = {{source}};
      RECENT_BOOKS.writable = false;
#endif
      assert(cm::updateMoveReferences() == cm::ReferenceResult::RecoveryRequired);
      assert(cj::scanRecords(journal, size, slot) == cj::ScanResult::Candidate);
      assert(Storage.lookupPath(target) == HalStorage::PathStatus::Present);
      assert(dc::discoverResumable(request, requestSize, requestSlot) == dc::FindResult::Found);
#if defined(SDFAT_REAL_MOVE_STORES)
      assert(Storage.remove("/.crosspoint/recent.json.tmp"));
      APP_STATE.openEpubPath = "/other.epub";
      assert(cm::updateMoveReferences() == cm::ReferenceResult::RecoveryRequired);
      assert(cj::scanRecords(journal, size, slot) == cj::ScanResult::Candidate);
      APP_STATE.openEpubPath = source;
#else
      RECENT_BOOKS.writable = true;
#endif
      assert(cm::updateMoveReferences() == cm::ReferenceResult::Updated);
      assert(dc::discoverResumable(request, requestSize, requestSlot) == dc::FindResult::Missing);
#if defined(SDFAT_REAL_MOVE_STORES)
      assert(APP_STATE.openEpubPath == target);
      assert(APP_STATE.favoriteSleepImagePath == "/favorite.bmp" && APP_STATE.readerActivityLoadCount == 7);
      const auto& books = RECENT_BOOKS.getBooks();
      assert(books.size() == (removeRecent ? 1 : 2));
      assert(books.back().path == "/other.epub" && books.back().title == "Other" && books.back().author == "Else");
      if (!removeRecent)
        assert(books[0].path == target && books[0].title == "Book" && books[0].author == "Writer" &&
               books[0].coverBmpPath == "/.crosspoint/old-cover.bmp");
      for (const auto& path : {"/.crosspoint/state.json", "/.crosspoint/recent.json"}) {
        auto file = Storage.open(path, O_RDONLY);
        assert(file);
        JsonDocument doc;
        std::string bytes(file.fileSize(), '\0');
        assert(file.read(bytes.data(), bytes.size()) == static_cast<int>(bytes.size()) && file.close());
        assert(!deserializeJson(doc, bytes));
        if (std::string_view(path).ends_with("state.json")) {
          assert(doc["openEpubPath"] == target);
          assert(doc["favoriteSleepImagePath"] == "/favorite.bmp" && doc["readerActivityLoadCount"] == 7);
        } else {
          assert(doc["books"].size() == (removeRecent ? 1 : 2));
          assert(doc["books"][0]["path"] == (removeRecent ? "/other.epub" : target));
        }
      }
#else
      assert(APP_STATE.openEpubPath == target && RECENT_BOOKS.books[0].path == target);
#endif
      assert(Storage.lookupPath(source) == HalStorage::PathStatus::Missing);
      assert(Storage.lookupPath(target) == HalStorage::PathStatus::Present);
      assert(cj::scanRecords(journal, size, slot) == cj::ScanResult::Empty);
    }
    FsVolume volume;
    assert(volume.begin(&integrated));
    Storage.attach(volume);
    assert(cj::retiredDeferredMoveProof(source, 0x12345678, moveHash) == cj::DeferredRetirementProof::Retired);
    assert(cm::verifyRetiredFiles(source, target, 0x12345678, moveHash) == cm::VerifyFilesResult::ContentMatches);
#if defined(SDFAT_REAL_MOVE_STORES)
    movedstate::checkBookmarks(target, removeRecent ? 4 : 2);
    movedstate::checkProgress(target, progressFormat);
    for (const auto& path : {"/.crosspoint/state.json", "/.crosspoint/recent.json"}) {
      auto file = Storage.open(path, O_RDONLY);
      assert(file);
      std::string bytes(file.fileSize(), '\0');
      assert(file.read(bytes.data(), bytes.size()) == static_cast<int>(bytes.size()) && file.close());
      JsonDocument doc;
      assert(!deserializeJson(doc, bytes));
      if (std::string_view(path).ends_with("state.json")) {
        assert(doc["openEpubPath"] == target);
        assert(doc["favoriteSleepImagePath"] == "/favorite.bmp" && doc["readerActivityLoadCount"] == 7);
      } else {
        assert(doc["books"].size() == (removeRecent ? 1 : 2));
        assert(doc["books"][0]["path"] == (removeRecent ? "/other.epub" : target));
      }
    }
#endif
  }
  int moveFaults = 0;
  for (int fault = 0; fault < 16; ++fault) {
    Disk trial;
    trial.sectors = beforeMove;
    {
      FsVolume volume;
      assert(volume.begin(&trial));
      Storage.attach(volume);
      APP_STATE.openEpubPath = source;
#if defined(SDFAT_REAL_MOVE_STORES)
      JsonDocument recent;
      recent["books"].to<JsonArray>().add<JsonObject>()["path"] = source;
      assert(RECENT_BOOKS.fromJson(recent.as<JsonVariantConst>()));
#else
      RECENT_BOOKS.books = {{source}};
      RECENT_BOOKS.writable = true;
#endif
      trial.failAt = trial.writes + fault;
      trial.failStop = true;
      (void)cm::updateMoveReferences();
    }
    if (!trial.injected) break;
    ++moveFaults;
    trial.dead = false;
    trial.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&trial));
    Storage.attach(volume);
    bool bookPreserved = false;
    for (const char* path : {source, target}) {
      auto book = Storage.open(path, O_RDONLY);
      if (!book) continue;
      const uint8_t expected[] = {1, 3, 5, 7};
      uint8_t contents[sizeof(expected)]{};
      const bool matches = book.read(contents, sizeof(contents)) == sizeof(contents) &&
                           std::memcmp(contents, expected, sizeof(expected)) == 0;
      assert(book.close());
      bookPreserved |= matches;
    }
    assert(bookPreserved);
    assert(Storage.lookupPath("/.completion-journal/.completion-00") == HalStorage::PathStatus::Present);
  }
  assert(moveFaults > 0);
  std::printf("%s: %d integrated move write faults retained book and journal\n", exfat ? "exFAT" : "FAT", moveFaults);
  int receiptFaults = 0, receiptRecovered = 0;
  for (int fault = 0; fault < 32; ++fault) {
    Disk trial;
    trial.sectors = beforeRetirement;
    {
      FsVolume volume;
      assert(volume.begin(&trial));
      Storage.attach(volume);
      trial.failAt = trial.writes + fault;
      trial.failStop = true;
      (void)dc::retireMovedIntent(source);
    }
    if (!trial.injected) break;
    ++receiptFaults;
    trial.dead = false;
    trial.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&trial));
    Storage.attach(volume);
    auto book = Storage.open(target, O_RDONLY);
    const uint8_t expected[] = {1, 3, 5, 7};
    uint8_t contents[sizeof(expected)]{};
    assert(book && book.read(contents, sizeof(contents)) == sizeof(contents) && book.close());
    assert(std::memcmp(contents, expected, sizeof(expected)) == 0);
    cj::Record pending{};
    size_t size = 0;
    unsigned slot = 99;
    assert(cj::scanRecords(pending, size, slot) == cj::ScanResult::Candidate);
    const auto recovered = dc::retireMovedIntent(source);
    if (recovered == dc::RetireMoveResult::Retired || recovered == dc::RetireMoveResult::AlreadyRetired)
      ++receiptRecovered;
    else {
      dc::Record deferred{};
      size_t deferredSize = 0;
      unsigned deferredSlot = 99;
      const auto found = dc::findForMoveRecovery(source, deferred, deferredSize, deferredSlot);
      const auto verified = cm::verifyPreparedFiles();
      const auto receiptStatus = Storage.lookupPath("/.completion-deferred/.dretire-00");
      std::printf("%s receipt fault %d retry=%d find=%d verify=%d receipt=%d\n", exfat ? "exFAT" : "FAT", fault,
                  static_cast<int>(recovered), static_cast<int>(found), static_cast<int>(verified),
                  static_cast<int>(receiptStatus));
    }
  }
  assert(receiptFaults > 0 && receiptRecovered > 0);
  std::printf("%s: %d interrupted receipt writes retained book and journal; %d retries recovered\n",
              exfat ? "exFAT" : "FAT", receiptFaults, receiptRecovered);
  const auto baseline = disk.sectors;
  int injected = 0, matched = 0;
  for (int fault = 0; fault < 128; ++fault) {
    Disk trial;
    trial.sectors = baseline;
    FsVolume volume;
    assert(volume.begin(&trial));
    Storage.attach(volume);
    trial.failReadAt = trial.reads + fault;
    const int before = trial.writes;
    const auto outcome = cm::verifyPreparedFiles();
    assert(trial.writes == before);
    if (!trial.injectedRead) break;
    ++injected;
    if (outcome == cm::VerifyFilesResult::ContentMatches) ++matched;
  }
  assert(injected > 0 && matched == 0);
  std::printf("%s: %d injected read faults, %d content matches\n", exfat ? "exFAT" : "FAT", injected, matched);
  // Re-mount first so cached directory/data blocks cannot mask corruption.
  {
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    assert(cm::verifyPreparedFiles() == cm::VerifyFilesResult::ContentMatches);
    assert(dc::retireMovedIntent(source) == dc::RetireMoveResult::AlreadyRetired);
    auto file = Storage.open(target, O_RDWR);
    assert(file);
    const uint8_t changed[] = {2, 3, 5, 7};
    assert(file.write(changed, sizeof(changed)) == sizeof(changed) && file.sync() && file.close());
    const int before = disk.writes;
    assert(cm::verifyPreparedFiles() == cm::VerifyFilesResult::NotReady);
    assert(disk.writes == before);
  }
  {
    Disk trial;
    trial.sectors = baseline;
    uint64_t moveHash = 0;
    {
      FsVolume volume;
      assert(volume.begin(&trial));
      Storage.attach(volume);
      cj::Record move{};
      size_t size = 0;
      unsigned slot = 99;
      assert(cj::scanRecords(move, size, slot) == cj::ScanResult::Candidate);
      moveHash = cj::checksum(move.data(), size);
      assert(cj::retire() == cj::RetirementResult::Retired);
    }
    retiredBaseline = trial.sectors;
    retiredHash = moveHash;
    FsVolume volume;
    assert(volume.begin(&trial));
    Storage.attach(volume);
    assert(cj::retiredDeferredMoveProof(source, 0x12345678, moveHash) == cj::DeferredRetirementProof::Retired);
    assert(cj::retiredDeferredMoveProof(source, 0x12345679, moveHash) == cj::DeferredRetirementProof::Missing);
    assert(cm::inspectRetiredPlan(source, target, 0x12345678, moveHash) == cm::RetiredPlanResult::Ready);
    assert(cm::inspectRetiredPlan(source, "/read/other.epub", 0x12345678, moveHash) == cm::RetiredPlanResult::Conflict);
    cj::Record duplicate{};
    size_t duplicateSize = 0;
    assert(cj::inspectRecord(0, duplicate, duplicateSize) == cj::InspectResult::Valid);
    assert(cj::createRecord(1, duplicate, duplicateSize) == cj::CreateResult::Created);
    assert(cj::createSeal(1, duplicate, duplicateSize) == cj::CreateResult::Created);
    cj::Seal duplicateReceipt{};
    assert(cj::encodeRetirement(1, duplicate, duplicateSize, duplicateReceipt));
    auto receipt = Storage.open("/.completion-journal/.cretire-01", O_CREAT | O_EXCL | O_WRONLY);
    assert(receipt && receipt.write(duplicateReceipt.data(), duplicateReceipt.size()) == duplicateReceipt.size() &&
           receipt.sync() && receipt.close());
    assert(cj::retiredDeferredMoveProof(source, 0x12345678, moveHash) == cj::DeferredRetirementProof::Conflict);
    assert(Storage.remove("/.completion-journal/.completion-01"));
    assert(Storage.remove("/.completion-journal/.cseal-01"));
    assert(Storage.remove("/.completion-journal/.cretire-01"));
    const int before = trial.writes;
    assert(cm::verifyRetiredFiles(source, target, 0x12345678, moveHash) == cm::VerifyFilesResult::ContentMatches);
    assert(trial.writes == before);
    auto changed = Storage.open(target, O_RDWR);
    const uint8_t replacement = 42;
    assert(changed && changed.write(&replacement, 1) == 1 && changed.sync() && changed.close());
    assert(cm::verifyRetiredFiles(source, target, 0x12345678, moveHash) == cm::VerifyFilesResult::NotReady);
  }
  int retiredReadFaults = 0;
  for (int fault = 0; fault < 16; ++fault) {
    Disk trial;
    trial.sectors = retiredBaseline;
    FsVolume volume;
    assert(volume.begin(&trial));
    Storage.attach(volume);
    trial.failReadAt = trial.reads + fault;
    const int before = trial.writes;
    const auto result = cm::verifyRetiredFiles(source, target, 0x12345678, retiredHash);
    assert(trial.writes == before);
    if (!trial.injectedRead) break;
    ++retiredReadFaults;
    assert(result != cm::VerifyFilesResult::ContentMatches);
  }
  assert(retiredReadFaults > 0);
  std::printf("%s: %d retired-file read faults never reported a match\n", exfat ? "exFAT" : "FAT", retiredReadFaults);
  std::printf("%s: read-only move verification checks passed after remount and mutation\n", exfat ? "exFAT" : "FAT");
}

void moveFileFaults(bool exfat, bool failStop, bool copy) {
  namespace cm = readingstats::completionmove;
  Disk baseline;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  } else {
    FatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  }
  constexpr char source[] = "/books/source.epub";
  constexpr char target[] = "/read/destination.epub";
  cm::FileIdentity identity;
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    assert(volume.mkdir("/books", true) && volume.mkdir("/read", true) && volume.mkdir("/.crosspoint", true));
    auto book = Storage.open(source, O_CREAT | O_EXCL | O_WRONLY);
    assert(book);
    std::array<uint8_t, 512> data{};
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>((i * 37) & 0xff);
    for (int n = 0; n < 32; ++n) assert(book.write(data.data(), data.size()) == data.size());
    assert(book.sync() && book.close());
    auto progress = Storage.open(FIXTURE, O_CREAT | O_EXCL | O_WRONLY);
    assert(progress);
    const uint8_t marker[] = {12, 34, 56, 78};
    assert(progress.write(marker, sizeof(marker)) == sizeof(marker));
    assert(progress.sync() && progress.close());
    assert(cm::captureFile(source, identity) == cm::FileResult::Ready);
  }
  int injected = 0, recovered = 0, manual = 0;
  for (int fault = -1; fault < 150; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    cm::FileResult result;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      disk.failAt = fault;
      disk.failStop = failStop;
      result = copy ? cm::copyFileChecked(source, target, identity) : cm::moveFileChecked(source, target, identity);
    }
    if (fault >= 0 && !disk.injected) break;
    if (disk.injected) ++injected;
    if (fault == -1) assert(result == (copy ? cm::FileResult::Copied : cm::FileResult::Moved));
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    // Force later allocation before checking the original clusters, so a
    // stale/free cluster cannot appear safe merely because it is unclaimed.
    auto allocation = Storage.open("/allocation.bin", O_CREAT | O_EXCL | O_WRONLY);
    assert(allocation);
    std::array<uint8_t, 512> occupied{};
    occupied.fill(0xa7);
    for (int n = 0; n < 64; ++n) assert(allocation.write(occupied.data(), occupied.size()) == occupied.size());
    assert(allocation.sync() && allocation.close());
    cm::FileIdentity oldFile, newFile;
    const auto oldStatus = cm::captureFile(source, oldFile);
    const auto newStatus = cm::captureFile(target, newFile);
    const auto matches = [&](cm::FileResult status, const cm::FileIdentity& found) {
      return status == cm::FileResult::Ready && found.present && found.size == identity.size &&
             found.checksum == identity.checksum;
    };
    if (copy)
      assert(matches(oldStatus, oldFile));
    else
      assert(matches(oldStatus, oldFile) || matches(newStatus, newFile));
    auto progress = Storage.open(FIXTURE, O_RDONLY);
    assert(progress);
    uint8_t marker[4]{};
    assert(progress.read(marker, sizeof(marker)) == sizeof(marker) && progress.close());
    assert(marker[0] == 12 && marker[1] == 34 && marker[2] == 56 && marker[3] == 78);
    const auto retried =
        copy ? cm::copyFileChecked(source, target, identity) : cm::moveFileChecked(source, target, identity);
    if (retried == cm::FileResult::Copied || retried == cm::FileResult::AlreadyCopied ||
        retried == cm::FileResult::Moved || retried == cm::FileResult::AlreadyMoved) {
      ++recovered;
      cm::FileIdentity after;
      assert(matches(cm::captureFile(target, after), after));
    } else {
      ++manual;
      std::printf("%s %s %s fault %d retry=%d source=%d destination=%d\n", exfat ? "exFAT" : "FAT",
                  failStop ? "fail-stop" : "transient", copy ? "copy" : "move", fault, static_cast<int>(retried),
                  static_cast<int>(oldStatus), static_cast<int>(newStatus));
      if (!copy && oldStatus == cm::FileResult::Ready && newStatus == cm::FileResult::Ready) {
        auto root = Storage.openBatchRoot();
        FsFile a, b;
        assert(root && a.open(&root, source + 1, O_RDONLY) && b.open(&root, target + 1, O_RDONLY));
        const auto first = a.firstCluster(), second = b.firstCluster();
        std::printf("  duplicate first clusters: %lu, %lu; checked disjoint=%d\n", static_cast<unsigned long>(first),
                    static_cast<unsigned long>(second), settingsFilesHaveDisjointChains(a, b));
        assert(a.close() && b.close() && root.close());
        if (first != 0 && first == second && matches(oldStatus, oldFile) && matches(newStatus, newFile)) {
          assert(cm::inspectMoveFile(source, target, identity) == cm::MoveFileState::Duplicate);
#if defined(SDFAT_ALIAS_EXPERIMENT)
          experimentalAliasFaults(disk, exfat, failStop, source, target, identity);
          Storage.attach(volume);
          experimentalAliasReadFaults(disk, exfat, source, target, identity);
          Storage.attach(volume);
#if defined(SDFAT_ALIAS_ONE_SECTOR)
          if (!exfat) {
            experimentalTornAliasWrites(disk, source, target, identity);
            Storage.attach(volume);
          }
#endif
#endif
          // Demonstrate why ordinary remove is unsafe on these duplicate
          // entries, using an isolated clone of the synthetic disk only.
          Disk clone;
          clone.sectors = disk.sectors;
          {
            FsVolume cloneVolume;
            assert(cloneVolume.begin(&clone));
            Storage.attach(cloneVolume);
            if (Storage.remove(source)) {
              auto cloneRoot = Storage.openBatchRoot();
              FsFile survivor;
              assert(cloneRoot && survivor.open(&cloneRoot, target + 1, O_RDONLY));
              const bool valid = settingsFileHasCheckedChain(survivor);
              assert(survivor.close() && cloneRoot.close());
              assert(!valid);
              std::printf("  removing one alias invalidated the surviving chain\n");
            }
          }
          Storage.attach(volume);
        }
      }
    }
    cm::FileIdentity oldAfter, newAfter;
    const auto oldAfterStatus = cm::captureFile(source, oldAfter);
    const auto newAfterStatus = cm::captureFile(target, newAfter);
    if (copy)
      assert(matches(oldAfterStatus, oldAfter));
    else
      assert(matches(oldAfterStatus, oldAfter) || matches(newAfterStatus, newAfter));
    progress = Storage.open(FIXTURE, O_RDONLY);
    assert(progress);
    std::memset(marker, 0, sizeof(marker));
    assert(progress.read(marker, sizeof(marker)) == sizeof(marker) && progress.close());
    assert(marker[0] == 12 && marker[1] == 34 && marker[2] == 56 && marker[3] == 78);
  }
  assert(injected > 0);
  std::printf("%s %s: %d %s write faults retained required files\n", exfat ? "exFAT" : "FAT",
              failStop ? "fail-stop" : "transient", injected, copy ? "copy" : "move");
  std::printf("%s %s: %d %s retries succeeded, %d require recovery\n", exfat ? "exFAT" : "FAT",
              failStop ? "fail-stop" : "transient", recovered, copy ? "copy" : "move", manual);
}

#if defined(SDFAT_CLEANUP_EXPERIMENT)
void cleanupFaults(bool exfat, bool failStop) {
#if defined(SDFAT_CLEANUP_INPLACE)
  namespace ce = cleanupinplace;
#else
  namespace ce = cleanupexperiment;
#endif
  constexpr char directory[] = "/.crosspoint/reading_stats/epub_123";
  constexpr char bookPath[] = "/.crosspoint/reading_stats/epub_123/stats_v5.bin";
  constexpr char globalPath[] = "/.crosspoint/global_stats.bin";
  Disk baseline;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  } else {
    FatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  }
  cj::Baseline previous{};
  previous.book[0] = 5;
  previous.global[0] = 3;
  previous.bookSize = cj::BOOK_BYTES;
  previous.globalSize = cj::GLOBAL_BYTES;
  auto nextBook = previous.book;
  auto nextGlobal = previous.global;
  nextBook[11] = 1;
  nextGlobal[13] = 1;
  cj::Record journal{};
  size_t journalSize = 0;
  assert(cj::encode(directory, nextBook, nextGlobal, previous, journal, journalSize));
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    assert(volume.mkdir(directory, true) && volume.mkdir("/.completion-journal", true));
    const auto write = [](const char* name, const uint8_t* bytes, size_t size) {
      auto file = Storage.open(name, O_CREAT | O_EXCL | O_WRONLY);
      assert(file && file.write(bytes, size) == size && file.sync() && file.close());
    };
    write(bookPath, previous.book.data(), previous.bookSize);
    write(globalPath, previous.global.data(), previous.globalSize);
    const uint8_t progress[] = {12, 34, 56, 78};
    write(FIXTURE, progress, sizeof(progress));
    assert(cj::start(journal, journalSize) == cj::StartResult::Started);
    assert(cj::replay() == cj::ReplayResult::AppliedUnretired);
    assert(cj::retire() == cj::RetirementResult::Retired);
#if defined(SDFAT_CLEANUP_INPLACE)
    // Test reuse of a marker left by a different retired transaction, not
    // only first-time creation of the marker.
    assert(ce::reclaim(0) == ce::Result::Reclaimed);
    // A byte-identical transaction may recur. Its stale marker must not
    // authorize deleting a new, unretired journal with the same checksum.
    const auto replace = [](const char* name, const uint8_t* bytes, size_t size) {
      auto file = Storage.open(name, O_WRONLY | O_TRUNC);
      assert(file && file.write(bytes, size) == size && file.sync() && file.close());
    };
    replace(bookPath, previous.book.data(), previous.bookSize);
    replace(globalPath, previous.global.data(), previous.globalSize);
    assert(cj::start(journal, journalSize) == cj::StartResult::Started);
    assert(ce::reclaim(0) == ce::Result::Refused);
    assert(cj::replay() == cj::ReplayResult::AppliedUnretired);
    assert(ce::reclaim(0) == ce::Result::Refused);
    assert(cj::retire() == cj::RetirementResult::Retired);
    assert(ce::reclaim(0) == ce::Result::Reclaimed);
    cj::Baseline secondBefore{};
    secondBefore.book = nextBook;
    secondBefore.global = nextGlobal;
    secondBefore.bookSize = cj::BOOK_BYTES;
    secondBefore.globalSize = cj::GLOBAL_BYTES;
    nextBook[11] = 0;
    nextGlobal[13] = 2;
    assert(cj::encode(directory, nextBook, nextGlobal, secondBefore, journal, journalSize));
    assert(cj::start(journal, journalSize) == cj::StartResult::Started);
    assert(ce::reclaim(0) == ce::Result::Refused);
    assert(cj::replay() == cj::ReplayResult::AppliedUnretired);
    assert(ce::reclaim(0) == ce::Result::Refused);
    assert(cj::retire() == cj::RetirementResult::Retired);
#endif
  }
  const auto verifyData = [&]() {
    std::array<uint8_t, cj::GLOBAL_BYTES> bytes{};
    auto file = Storage.open(bookPath, O_RDONLY);
    assert(file && file.fileSize() == nextBook.size());
    assert(file.read(bytes.data(), nextBook.size()) == nextBook.size() && file.close());
    assert(std::memcmp(bytes.data(), nextBook.data(), nextBook.size()) == 0);
    file = Storage.open(globalPath, O_RDONLY);
    assert(file && file.fileSize() == nextGlobal.size());
    assert(file.read(bytes.data(), nextGlobal.size()) == nextGlobal.size() && file.close());
    assert(std::memcmp(bytes.data(), nextGlobal.data(), nextGlobal.size()) == 0);
    file = Storage.open(FIXTURE, O_RDONLY);
    assert(file);
    uint8_t progress[4]{};
    assert(file.read(progress, sizeof(progress)) == sizeof(progress) && file.close());
    assert(progress[0] == 12 && progress[1] == 34 && progress[2] == 56 && progress[3] == 78);
  };
  int faults = 0, recovered = 0, unresolved = 0;
  for (int fault = -1; fault < 80; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      disk.failAt = fault;
      disk.failStop = failStop;
      const auto result = ce::reclaim(0);
      if (fault == -1) assert(result == ce::Result::Reclaimed);
    }
    if (fault >= 0 && !disk.injected) break;
    if (disk.injected) ++faults;
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    auto allocation = Storage.open("/cleanup-allocation.bin", O_CREAT | O_EXCL | O_WRONLY);
    assert(allocation);
    std::array<uint8_t, 512> fill{};
    fill.fill(0x9b);
    for (int n = 0; n < 64; ++n) assert(allocation.write(fill.data(), fill.size()) == fill.size());
    assert(allocation.sync() && allocation.close());
    verifyData();
    const auto retry = ce::reclaim(0);
    if (retry == ce::Result::Reclaimed || retry == ce::Result::Missing) {
      if (disk.injected) ++recovered;
    } else if (disk.injected) {
      ++unresolved;
      std::printf("%s %s cleanup fault %d retry=%d marker=%d record=%d seal=%d receipt=%d\n", exfat ? "exFAT" : "FAT",
                  failStop ? "fail-stop" : "transient", fault, static_cast<int>(retry),
                  static_cast<int>(Storage.lookupPath("/.completion-journal/.cclean-00")),
                  static_cast<int>(Storage.lookupPath("/.completion-journal/.completion-00")),
                  static_cast<int>(Storage.lookupPath("/.completion-journal/.cseal-00")),
                  static_cast<int>(Storage.lookupPath("/.completion-journal/.cretire-00")));
    }
    verifyData();
  }
  assert(faults > 0);
  assert(recovered + unresolved == faults);
  std::printf("%s %s: cleanup %d injected write faults, %d retries complete, %d unresolved\n", exfat ? "exFAT" : "FAT",
              failStop ? "fail-stop" : "transient", faults, recovered, unresolved);

  int tornTrials = 0, tornRecovered = 0;
  for (int fault = 0; fault < 40; ++fault) {
    bool any = false;
    for (const int bytes : {0, 1, 16, 32, 64, 128, 256, 511}) {
      Disk disk;
      disk.sectors = baseline.sectors;
      {
        FsVolume volume;
        assert(volume.begin(&disk));
        Storage.attach(volume);
        disk.failAt = fault;
        disk.failStop = true;
        disk.tornAfter = bytes;
        ce::reclaim(0);
      }
      if (!disk.injected) continue;
      any = true;
      ++tornTrials;
      disk.dead = false;
      disk.failAt = -1;
      disk.tornAfter = -1;
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      auto allocation = Storage.open("/cleanup-torn.bin", O_CREAT | O_EXCL | O_WRONLY);
      assert(allocation);
      std::array<uint8_t, 512> fill{};
      fill.fill(0x9c);
      for (int n = 0; n < 64; ++n) assert(allocation.write(fill.data(), fill.size()) == fill.size());
      assert(allocation.sync() && allocation.close());
      verifyData();
      const auto retry = ce::reclaim(0);
      if (retry == ce::Result::Reclaimed || retry == ce::Result::Missing) ++tornRecovered;
      verifyData();
    }
    if (!any) break;
  }
  assert(tornTrials > 0);
  std::printf("%s %s: cleanup %d torn-write trials, %d retries complete\n", exfat ? "exFAT" : "FAT",
              failStop ? "fail-stop" : "transient", tornTrials, tornRecovered);

  // Exercise more than the journal's 64 slots without a restart or faults.
  Disk cycles;
  cycles.sectors = baseline.sectors;
  {
    FsVolume volume;
    assert(volume.begin(&cycles));
    Storage.attach(volume);
    assert(ce::reclaim(0) == ce::Result::Reclaimed);
    auto beforeBook = nextBook;
    auto beforeGlobal = nextGlobal;
    for (unsigned i = 0; i < 70; ++i) {
      cj::Baseline before{};
      before.book = beforeBook;
      before.global = beforeGlobal;
      before.bookSize = cj::BOOK_BYTES;
      before.globalSize = cj::GLOBAL_BYTES;
      auto afterBook = beforeBook;
      auto afterGlobal = beforeGlobal;
      afterBook[11] ^= 1;
      afterGlobal[13] ^= 1;
      cj::Record record{};
      size_t size = 0;
      assert(cj::encode(directory, afterBook, afterGlobal, before, record, size));
      assert(cj::start(record, size) == cj::StartResult::Started);
      assert(cj::replay() == cj::ReplayResult::AppliedUnretired);
      assert(cj::retire() == cj::RetirementResult::Retired);
      assert(ce::reclaim(0) == ce::Result::Reclaimed);
      beforeBook = afterBook;
      beforeGlobal = afterGlobal;
    }
  }
  std::printf("%s: reclaimed the same slot across 70 consecutive transactions\n", exfat ? "exFAT" : "FAT");
}
#endif

#if defined(SDFAT_IMPORT_SNAPSHOT)
void importSnapshotFaults(bool exfat, bool failStop) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string contents = "{\"fontSize\":14,\"data\":\"" + std::string(1024, 'v') + "\"}";
  Disk baseline;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  } else {
    FatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  }
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    assert(volume.mkdir("/.crosspoint", true));
    auto file = Storage.open(primary, O_CREAT | O_EXCL | O_WRONLY);
    assert(file && file.write(contents.data(), contents.size()) == contents.size() && file.sync() && file.close());
    file = Storage.open("/progress.bin", O_CREAT | O_EXCL | O_WRONLY);
    const uint8_t progress[] = {12, 34, 56, 78};
    assert(file && file.write(progress, sizeof(progress)) == sizeof(progress) && file.sync() && file.close());
  }
  const auto verify = [&](const char* path, const char* expected, size_t size) {
    auto file = Storage.open(path, O_RDONLY);
    assert(file && file.fileSize() == size);
    std::string data(size, '\0');
    assert(file.read(data.data(), size) == static_cast<int>(size) && file.close());
    assert(std::memcmp(data.data(), expected, size) == 0);
  };
  int injected = 0;
  for (int fault = -1; fault < 100; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    bool saved = false;
    char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      disk.failAt = fault;
      disk.failStop = failStop;
      saved = crossinkimport::snapshotCurrentSettings(contents.data(), contents.size(), snapshot);
    }
    if (fault >= 0 && !disk.injected) break;
    if (fault >= 0) ++injected;
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    const char progress[] = {12, 34, 56, 78};
    verify(primary, contents.data(), contents.size());
    verify("/progress.bin", progress, sizeof(progress));
    auto extra = Storage.open("/after-snapshot.bin", O_CREAT | O_EXCL | O_WRONLY);
    assert(extra);
    std::array<uint8_t, 512> fill{};
    fill.fill(0x8b);
    for (int n = 0; n < 64; ++n) assert(extra.write(fill.data(), fill.size()) == fill.size());
    assert(extra.sync() && extra.close());
    verify(primary, contents.data(), contents.size());
    verify("/progress.bin", progress, sizeof(progress));
    if (saved) verify(snapshot, contents.data(), contents.size());
    if (fault == -1) assert(saved);
  }
  assert(injected > 0);
  std::printf("%s %s: %d snapshot write faults retained settings and progress\n", exfat ? "exFAT" : "FAT",
              failStop ? "fail-stop" : "transient", injected);
  int tornTrials = 0;
  for (int fault = 0; fault < 40; ++fault) {
    bool any = false;
    for (const int bytes : {0, 1, 16, 32, 64, 128, 256, 511}) {
      Disk disk;
      disk.sectors = baseline.sectors;
      char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
      bool saved = false;
      {
        FsVolume volume;
        assert(volume.begin(&disk));
        Storage.attach(volume);
        disk.failAt = fault;
        disk.failStop = true;
        disk.tornAfter = bytes;
        saved = crossinkimport::snapshotCurrentSettings(contents.data(), contents.size(), snapshot);
      }
      if (!disk.injected) continue;
      any = true;
      ++tornTrials;
      disk.dead = false;
      disk.failAt = -1;
      disk.tornAfter = -1;
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      const char progress[] = {12, 34, 56, 78};
      verify(primary, contents.data(), contents.size());
      verify("/progress.bin", progress, sizeof(progress));
      auto extra = Storage.open("/after-snapshot.bin", O_CREAT | O_EXCL | O_WRONLY);
      assert(extra);
      std::array<uint8_t, 512> fill{};
      fill.fill(0x8b);
      for (int n = 0; n < 64; ++n) assert(extra.write(fill.data(), fill.size()) == fill.size());
      assert(extra.sync() && extra.close());
      verify(primary, contents.data(), contents.size());
      verify("/progress.bin", progress, sizeof(progress));
      if (saved) verify(snapshot, contents.data(), contents.size());
    }
    if (!any) break;
  }
  assert(tornTrials > 0);
  std::printf("%s: %d sampled torn snapshot writes retained settings and progress\n", exfat ? "exFAT" : "FAT",
              tornTrials);
}
#endif

#if defined(SDFAT_IMPORT_TRANSACTION)
void importTransactionFaults(bool exfat, bool failStop) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  constexpr char source[] = "/.crosspoint/crossink-settings.json";
  const std::string old = R"({"readerUpTap":2,"pluginsEnabled":1})";
  const std::string legacy = R"({"sideButtonOrientationAware":0,"sideButtonUpShort":7})";
  Disk baseline;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  } else {
    FatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  }
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    assert(volume.mkdir("/.crosspoint", true));
    for (const auto& entry : {std::pair{primary, old}, std::pair{source, legacy}}) {
      auto file = Storage.open(entry.first, O_CREAT | O_EXCL | O_WRONLY);
      assert(file && file.write(entry.second.data(), entry.second.size()) == entry.second.size());
      assert(file.sync() && file.close());
    }
    auto file = Storage.open("/progress.bin", O_CREAT | O_EXCL | O_WRONLY);
    const uint8_t progress[] = {12, 34, 56, 78};
    assert(file && file.write(progress, sizeof(progress)) == sizeof(progress) && file.sync() && file.close());
  }
  const auto matches = [&](const char* path, const char* expected, size_t size) {
    auto file = Storage.open(path, O_RDONLY);
    if (!file || file.fileSize() != size) return false;
    std::string data(size, '\0');
    return file.read(data.data(), size) == static_cast<int>(size) && file.close() &&
           std::memcmp(data.data(), expected, size) == 0;
  };
  int injected = 0, uncertain = 0;
  for (int fault = -1; fault < 350; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
    auto result = crossinkimport::ApplyResult::Unavailable;
    std::string expectedNew;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      JsonDocument proposed;
      assert(!deserializeJson(proposed, old));
      disk.failAt = fault;
      disk.failStop = failStop;
      result = crossinkimport::publishControls(proposed, true, false, old.data(), old.size(), snapshot);
      serializeJson(proposed, expectedNew);
      if (fault == -1)
        std::printf("%s: import performed %d sector writes without injected faults\n", exfat ? "exFAT" : "FAT",
                    disk.writes);
    }
    if (fault >= 0 && !disk.injected) break;
    if (disk.injected) ++injected;
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    const char progress[] = {12, 34, 56, 78};
    assert(matches(source, legacy.data(), legacy.size()));
    assert(matches("/progress.bin", progress, sizeof(progress)));
    if (snapshot[0])
      assert(matches(snapshot, old.data(), old.size()));
    else
      assert(matches(primary, old.data(), old.size()));
    if (result == crossinkimport::ApplyResult::Published)
      assert(matches(primary, expectedNew.data(), expectedNew.size()));
    const auto guard = crossinkimport::inspectImports();
    if (result == crossinkimport::ApplyResult::Published) assert(guard == crossinkimport::GuardState::Clear);
    if (result == crossinkimport::ApplyResult::WriteUncertain && guard == crossinkimport::GuardState::Clear)
      assert(matches(primary, old.data(), old.size()) || matches(primary, expectedNew.data(), expectedNew.size()));
    auto extra = Storage.open("/after-import.bin", O_CREAT | O_EXCL | O_WRONLY);
    assert(extra);
    std::array<uint8_t, 512> fill{};
    fill.fill(0x8b);
    for (int n = 0; n < 64; ++n) assert(extra.write(fill.data(), fill.size()) == fill.size());
    assert(extra.sync() && extra.close());
    assert(matches(source, legacy.data(), legacy.size()));
    assert(matches("/progress.bin", progress, sizeof(progress)));
    if (snapshot[0])
      assert(matches(snapshot, old.data(), old.size()));
    else
      assert(matches(primary, old.data(), old.size()));
    if (result == crossinkimport::ApplyResult::Published)
      assert(matches(primary, expectedNew.data(), expectedNew.size()));
    if (result == crossinkimport::ApplyResult::WriteUncertain) ++uncertain;
    if (fault == -1) assert(result == crossinkimport::ApplyResult::Published);
  }
  assert(injected > 0);
  std::printf("%s %s: %d import faults retained original settings and progress; %d uncertain writes\n",
              exfat ? "exFAT" : "FAT", failStop ? "fail-stop" : "transient", injected, uncertain);
}

void importRecoveryFaults(bool exfat, bool failStop) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string old = R"({"readerUpTap":2,"pluginsEnabled":1})";
  const std::string changed = R"({"readerUpTap":7,"pluginsEnabled":1})";
  Disk baseline;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  } else {
    FatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  }
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    assert(volume.mkdir("/.crosspoint", true));
    auto file = Storage.open(primary, O_CREAT | O_EXCL | O_WRONLY);
    assert(file && file.write(old.data(), old.size()) == old.size() && file.sync() && file.close());
    file = Storage.open("/progress.bin", O_CREAT | O_EXCL | O_WRONLY);
    const uint8_t progress[] = {12, 34, 56, 78};
    assert(file && file.write(progress, sizeof(progress)) == sizeof(progress) && file.sync() && file.close());
    assert(crossinkimport::snapshotCurrentSettings(old.data(), old.size(), snapshot));
    crossinkimport::ImportMarker marker{};
    assert(crossinkimport::beginImport(snapshot, old.data(), old.size(), changed.data(), changed.size(), marker));
  }
  const auto matches = [&](const char* path, const char* expected, size_t size) {
    auto file = Storage.open(path, O_RDONLY);
    if (!file || file.fileSize() != size) return false;
    std::string data(size, '\0');
    return file.read(data.data(), size) == static_cast<int>(size) && file.close() &&
           std::memcmp(data.data(), expected, size) == 0;
  };
  int injected = 0, retried = 0;
  for (int fault = -1; fault < 350; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    crossinkimport::RecoveryResult result;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      disk.failAt = fault;
      disk.failStop = failStop;
      result = crossinkimport::recoverUnchangedImport();
    }
    if (fault >= 0 && !disk.injected) break;
    if (disk.injected) ++injected;
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    const char progress[] = {12, 34, 56, 78};
    assert(matches(primary, old.data(), old.size()) && matches(snapshot, old.data(), old.size()));
    assert(matches("/progress.bin", progress, sizeof(progress)));
    auto extra = Storage.open("/after-recovery.bin", O_CREAT | O_EXCL | O_WRONLY);
    assert(extra);
    std::array<uint8_t, 512> fill{};
    fill.fill(0x8b);
    for (int n = 0; n < 64; ++n) assert(extra.write(fill.data(), fill.size()) == fill.size());
    assert(extra.sync() && extra.close());
    assert(matches(primary, old.data(), old.size()) && matches(snapshot, old.data(), old.size()));
    assert(matches("/progress.bin", progress, sizeof(progress)));
    if (result == crossinkimport::RecoveryResult::Recovered)
      assert(crossinkimport::inspectImports() == crossinkimport::GuardState::Clear);
    else if (disk.injected) {
      const auto retry = crossinkimport::recoverUnchangedImport();
      if (retry == crossinkimport::RecoveryResult::Recovered)
        ++retried;
      else {
        auto done = Storage.open("/.crosspoint/crossink-import/.cid-00", O_RDONLY);
        const auto size = done ? static_cast<long long>(done.fileSize()) : -1;
        if (done) assert(done.close());
        std::printf("%s recovery fault %d retry=%d done-size=%lld guard=%d\n", exfat ? "exFAT" : "FAT", fault,
                    static_cast<int>(retry), size, static_cast<int>(crossinkimport::inspectImports()));
      }
    }
    if (fault == -1) assert(result == crossinkimport::RecoveryResult::Recovered);
  }
  assert(injected > 0);
  std::printf("%s %s: %d recovery faults retained settings and progress; %d retries recovered\n",
              exfat ? "exFAT" : "FAT", failStop ? "fail-stop" : "transient", injected, retried);
}
#endif

void emptyMoveCopies(bool exfat) {
  Disk disk;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter formatter;
    assert(formatter.format(&disk, scratch));
  } else {
    FatFormatter formatter;
    assert(formatter.format(&disk, scratch));
  }
  FsVolume volume;
  assert(volume.begin(&disk));
  Storage.attach(volume);
  for (const char* path : {"/empty-a", "/empty-b"}) {
    auto file = Storage.open(path, O_CREAT | O_EXCL | O_WRONLY);
    assert(file && file.sync() && file.close());
  }
  assert(!Storage.distinctMoveChains("/empty-a", "/EMPTY-A"));
  assert(Storage.distinctMoveChains("/empty-a", "/empty-b"));
  readingstats::completionmove::FileIdentity identity;
  assert(readingstats::completionmove::captureFile("/empty-a", identity) ==
         readingstats::completionmove::FileResult::Ready);
  assert(identity.present && identity.size == 0);
  assert(readingstats::completionmove::copyFileChecked("/empty-a", "/empty-copy", identity) ==
         readingstats::completionmove::FileResult::Copied);
  assert(readingstats::completionmove::copyFileChecked("/empty-a", "/empty-copy", identity) ==
         readingstats::completionmove::FileResult::AlreadyCopied);
  assert(Storage.distinctMoveChains("/empty-a", "/empty-copy"));
  auto large = Storage.open("/large-state", O_CREAT | O_EXCL | O_WRONLY);
  assert(large);
  for (auto& byte : scratch) byte = 0x5a;
  for (size_t i = 0; i < 200; ++i) assert(large.write(scratch, sizeof(scratch)) == sizeof(scratch));
  assert(large.sync() && large.close());
  assert(readingstats::completionmove::captureFile("/large-state", identity) ==
         readingstats::completionmove::FileResult::Ready);
  assert(readingstats::completionmove::copyFileChecked("/large-state", "/large-copy", identity) ==
         readingstats::completionmove::FileResult::Copied);
  assert(Storage.distinctMoveChains("/large-state", "/large-copy"));
  assert(!Storage.settingsChainEligible("/large-state"));
  large = Storage.open("/large-state", O_WRONLY | O_APPEND);
  assert(large);
  for (size_t i = 200; i < 512; ++i) assert(large.write(scratch, sizeof(scratch)) == sizeof(scratch));
  assert(large.sync() && large.close());
  assert(Storage.distinctMoveChains("/empty-a", "/large-state"));
  large = Storage.open("/large-state", O_WRONLY | O_APPEND);
  assert(large && large.write(scratch, sizeof(scratch)) == sizeof(scratch));
  assert(large.sync() && large.close());
  assert(!Storage.distinctMoveChains("/empty-a", "/large-state"));
  std::printf("%s empty move-copy distinguishes directory entries\n", exfat ? "exFAT" : "FAT");
  std::printf("%s 100 KiB move-copy preserves settings chain limit\n", exfat ? "exFAT" : "FAT");
}

void moveReferenceWriteFaults(bool exfat, bool failStop) {
  Disk baseline;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  } else {
    FatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  }
  constexpr char primary[] = "/.crosspoint/state.json";
  constexpr char backup[] = "/.crosspoint/state.json.bak";
  constexpr char archive[] = "/.crosspoint/state.json.move-history-0000";
  constexpr char old[] = "{\"openEpubPath\":\"/old.epub\"}";
  constexpr char next[] = "{\"openEpubPath\":\"/read/old.epub\"}";
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    assert(volume.mkdir("/.crosspoint", true));
    auto file = Storage.open(primary, O_CREAT | O_EXCL | O_WRONLY);
    assert(file && file.write(old, sizeof(old) - 1) == sizeof(old) - 1 && file.sync() && file.close());
    const uint8_t progress[] = {12, 34, 56, 78};
    file = Storage.open(FIXTURE, O_CREAT | O_EXCL | O_WRONLY);
    assert(file && file.write(progress, sizeof(progress)) == sizeof(progress) && file.sync() && file.close());
  }
  int failures = 0;
  for (int fault = -1; fault < 150; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    bool result = false;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      disk.failAt = fault;
      disk.failStop = failStop;
      result = movereference::write(primary, next, sizeof(next) - 1);
    }
    if (fault >= 0 && !disk.injected) break;
    if (disk.injected) ++failures;
    if (fault == -1) assert(result);
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    const auto matches = [](const char* path, const char* expected, size_t length) {
      auto file = Storage.open(path, O_RDONLY);
      if (!file) return false;
      char bytes[128]{};
      const bool same = file.fileSize() == length && file.read(bytes, length) == static_cast<int>(length) &&
                        !std::memcmp(bytes, expected, length);
      return file.close() && same;
    };
    assert(matches(primary, old, sizeof(old) - 1) || matches(backup, old, sizeof(old) - 1) ||
           matches(archive, old, sizeof(old) - 1));
    const uint8_t progress[] = {12, 34, 56, 78};
    auto file = Storage.open(FIXTURE, O_RDONLY);
    uint8_t read[sizeof(progress)]{};
    assert(file && file.read(read, sizeof(read)) == sizeof(read) && file.close());
    assert(!std::memcmp(read, progress, sizeof(read)));
    if (result) assert(matches(primary, next, sizeof(next) - 1) && matches(archive, old, sizeof(old) - 1));
  }
  assert(failures > 0);
  std::printf("%s %s move-reference write: %d injected writes retained old state and progress\n",
              exfat ? "exFAT" : "FAT", failStop ? "fail-stop" : "transient", failures);
}

void deferredPublishFaults(bool exfat, bool failStop) {
  namespace dc = readingstats::deferredcompletion;
  Disk baseline;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  } else {
    FatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  }
  constexpr char bookPath[] = "/.crosspoint/reading_stats/epub_123/stats_v5.bin";
  constexpr char globalPath[] = "/.crosspoint/global_stats.bin";
  constexpr char source[] = "/Authors/Writer/Book.epub";
  cj::Baseline previous{};
  previous.book[0] = 5;
  previous.book[11] = 1;
  previous.global[0] = 3;
  previous.bookSize = cj::BOOK_BYTES;
  previous.globalSize = cj::GLOBAL_BYTES;
  cj::Record record{};
  size_t size = 0;
  assert(cj::encode("/.crosspoint/reading_stats/epub_123", previous.book, previous.global, previous, record, size,
                    cj::DEFER_MOVE_TO_READ, source));
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    assert(volume.mkdir("/.crosspoint/reading_stats/epub_123", true));
    assert(volume.mkdir("/.completion-journal", true));
    const auto write = [](const char* path, const uint8_t* bytes, size_t length) {
      auto file = Storage.open(path, O_CREAT | O_EXCL | O_WRONLY);
      assert(file && file.write(bytes, length) == length && file.sync() && file.close());
    };
    write(bookPath, previous.book.data(), previous.bookSize);
    write(globalPath, previous.global.data(), previous.globalSize);
    const uint8_t progress[] = {12, 34, 56, 78};
    write(FIXTURE, progress, sizeof(progress));
    assert(cj::start(record, size) == cj::StartResult::Started);
  }
  int failures = 0;
  for (int fault = -1; fault < 500; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    dc::PublishResult result;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      disk.failAt = fault;
      disk.failStop = failStop;
      result = dc::publish(source);
    }
    if (fault >= 0 && !disk.injected) break;
    if (disk.injected) ++failures;
    if (fault == -1) {
      assert(result == dc::PublishResult::Published);
      std::printf("%s %s deferred publish baseline writes: %d\n", exfat ? "exFAT" : "FAT",
                  failStop ? "fail-stop" : "transient", disk.writes);
    }
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    cj::Record recovered{};
    size_t recoveredSize = 0;
    unsigned slot = 99;
    assert(cj::scanRecords(recovered, recoveredSize, slot) == cj::ScanResult::Candidate);
    assert(slot == 0 && recoveredSize == size && recovered == record);
    const auto check = [](const char* path, const uint8_t* expected, size_t length) {
      auto file = Storage.open(path, O_RDONLY);
      assert(file && file.fileSize() == length);
      std::array<uint8_t, cj::GLOBAL_BYTES> bytes{};
      assert(file.read(bytes.data(), length) == static_cast<int>(length) && file.close());
      assert(std::memcmp(bytes.data(), expected, length) == 0);
    };
    check(bookPath, previous.book.data(), previous.bookSize);
    check(globalPath, previous.global.data(), previous.globalSize);
    const uint8_t progress[] = {12, 34, 56, 78};
    check(FIXTURE, progress, sizeof(progress));
    if (result == dc::PublishResult::Published) {
      assert(dc::publish(source) == dc::PublishResult::AlreadyPublished);
      dc::Record intent{};
      size_t intentSize = 0;
      unsigned intentSlot = 99;
      assert(dc::findReady(source, intent, intentSize, intentSlot) == dc::FindResult::Busy);
      assert(dc::discoverReady(intent, intentSize, intentSlot) == dc::FindResult::Busy);
      assert(cj::retire() == cj::RetirementResult::Retired);
      assert(dc::findReady(source, intent, intentSize, intentSlot) == dc::FindResult::Found);
      assert(intentSlot == 0);
      dc::Record discovered{};
      size_t discoveredSize = 0;
      unsigned discoveredSlot = 99;
      assert(dc::discoverReady(discovered, discoveredSize, discoveredSlot) == dc::FindResult::Found);
      uint64_t origin = 0;
      std::string_view discoveredSource;
      assert(dc::decodeWithHash(discovered.data(), discoveredSize, origin, discoveredSource));
      assert(discoveredSource == source && discoveredSlot == intentSlot);
    }
  }
  assert(failures > 0);
  std::printf("%s %s deferred publish: %d injected writes retained journal and progress\n", exfat ? "exFAT" : "FAT",
              failStop ? "fail-stop" : "transient", failures);
}

void deferredSourceRemount(bool exfat) {
  Disk disk;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter formatter;
    assert(formatter.format(&disk, scratch));
  } else {
    FatFormatter formatter;
    assert(formatter.format(&disk, scratch));
  }
  cj::Book book{};
  cj::Global global{};
  book[0] = 5;
  book[11] = 1;
  global[0] = 3;
  const std::string source = "/" + std::string(cj::MAX_SOURCE_PATH - 1, 'x');
  cj::Record record{};
  size_t size = 0;
  assert(cj::encode("/.crosspoint/reading_stats/epub_123", book, global, {}, record, size, cj::DEFER_MOVE_TO_READ,
                    source));
  {
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    assert(volume.mkdir("/.completion-journal", true));
    assert(cj::createRecord(0, record, size) == cj::CreateResult::Created);
    assert(cj::createSeal(0, record, size) == cj::CreateResult::Created);
  }
  {
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    cj::Record recovered{};
    size_t recoveredSize = 0;
    unsigned slot = 99;
    assert(cj::scanRecords(recovered, recoveredSize, slot) == cj::ScanResult::Candidate);
    std::string_view recoveredSource;
    assert(slot == 0 && recoveredSize == size &&
           cj::decodeDeferredSource(recovered.data(), recoveredSize, recoveredSource));
    assert(recoveredSource == source);
  }
  constexpr uint64_t originHash = 0x0123456789abcdefULL;
  assert(cj::encode("/.crosspoint/reading_stats/epub_123", book, global, {}, record, size,
                    cj::MOVE_TO_READ | cj::MOVE_FROM_DEFERRED, source, originHash));
  {
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    assert(cj::createRecord(1, record, size) == cj::CreateResult::Created);
    assert(cj::createSeal(1, record, size) == cj::CreateResult::Created);
  }
  {
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    cj::Record recovered{};
    size_t recoveredSize = 0;
    assert(cj::inspectRecord(1, recovered, recoveredSize) == cj::InspectResult::Valid);
    assert(cj::inspectSeal(1, recovered, recoveredSize) == cj::SealResult::Matching);
    std::string_view recoveredSource;
    uint64_t recoveredHash = 0;
    assert(cj::decodeDeferredMove(recovered.data(), recoveredSize, recoveredSource, recoveredHash));
    assert(recoveredSource == source && recoveredHash == originHash);
  }
  std::printf("%s deferred source survived remount\n", exfat ? "exFAT" : "FAT");
}

void retiredIntentRecovery(bool exfat) {
  namespace dc = readingstats::deferredcompletion;
  Disk disk;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter formatter;
    assert(formatter.format(&disk, scratch));
  } else {
    FatFormatter formatter;
    assert(formatter.format(&disk, scratch));
  }
  constexpr char source[] = "/Authors/Writer/Book.epub";
  constexpr uint64_t origin = 0x12345678;
  dc::Record intent{};
  size_t intentSize = 0;
  assert(dc::encode(source, origin, intent, intentSize));
  dc::Seal seal{}, receipt{};
  assert(dc::encodeSeal(0, intent, intentSize, seal));
  assert(dc::encodeSeal(0, intent, intentSize, receipt, true));
  cj::Book book{};
  cj::Global global{};
  book[0] = 5;
  book[11] = 1;
  global[0] = 3;
  cj::Record journal{};
  size_t journalSize = 0;
  assert(cj::encode("/.crosspoint/reading_stats/epub_123", book, global, {}, journal, journalSize,
                    cj::MOVE_TO_READ | cj::MOVE_FROM_DEFERRED, source, origin));
  {
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    assert(volume.mkdir("/.completion-journal", true));
    assert(volume.mkdir("/.completion-deferred", true));
    assert(cj::createRecord(0, journal, journalSize) == cj::CreateResult::Created);
    assert(cj::createSeal(0, journal, journalSize) == cj::CreateResult::Created);
    const auto write = [](const char* path, const uint8_t* data, size_t size) {
      auto file = Storage.open(path, O_CREAT | O_EXCL | O_WRONLY);
      assert(file && file.write(data, size) == size && file.sync() && file.close());
    };
    write("/.completion-deferred/.defer-00", intent.data(), intentSize);
    write("/.completion-deferred/.dseal-00", seal.data(), seal.size());
    write("/.completion-deferred/.dretire-00", receipt.data(), receipt.size());
  }
  {
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    dc::Record recovered{};
    size_t recoveredSize = 0;
    unsigned slot = 99;
    assert(dc::find(source, recovered, recoveredSize, slot) == dc::FindResult::Missing);
    assert(dc::findForMoveRecovery(source, recovered, recoveredSize, slot) == dc::FindResult::Found);
    assert(slot == 0 && recoveredSize == intentSize && recovered == intent);
    auto damaged = Storage.open("/.completion-deferred/.dretire-00", O_WRONLY | O_TRUNC);
    const uint8_t byte = 1;
    assert(damaged && damaged.write(&byte, 1) == 1 && damaged.sync() && damaged.close());
    assert(dc::findForMoveRecovery(source, recovered, recoveredSize, slot) == dc::FindResult::Blocked);
  }
  std::printf("%s retired intent survived remount\n", exfat ? "exFAT" : "FAT");
}

void rolloverFaults(bool exfat, bool failStop) {
  Disk baseline;
  uint8_t scratch[512];
  if (exfat) {
    ExFatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  } else {
    FatFormatter formatter;
    assert(formatter.format(&baseline, scratch));
  }
  constexpr char bookPath[] = "/.crosspoint/reading_stats/epub_123/stats_v5.bin";
  constexpr char globalPath[] = "/.crosspoint/global_stats.bin";
  cj::Baseline previous{};
  previous.book[0] = 5;
  previous.global[0] = 3;
  previous.bookSize = cj::BOOK_BYTES;
  previous.globalSize = cj::GLOBAL_BYTES;
  cj::Record record{};
  size_t size = 0;
  assert(cj::encode("/.crosspoint/reading_stats/epub_123", previous.book, previous.global, previous, record, size));
  constexpr char deferredSource[] = "/Authors/Writer/Book.epub";
  cj::Record deferredRecord{};
  size_t deferredSize = 0;
  assert(cj::encode("/.crosspoint/reading_stats/epub_123", previous.book, previous.global, previous, deferredRecord,
                    deferredSize, cj::DEFER_MOVE_TO_READ, deferredSource));
  {
    FsVolume volume;
    assert(volume.begin(&baseline));
    Storage.attach(volume);
    assert(volume.mkdir("/.crosspoint/reading_stats/epub_123", true));
    assert(volume.mkdir("/.completion-journal", true));
    const auto write = [](const char* path, const uint8_t* bytes, size_t length) {
      auto file = Storage.open(path, O_CREAT | O_EXCL | O_WRONLY);
      assert(file && file.write(bytes, length) == length && file.sync() && file.close());
    };
    write(bookPath, previous.book.data(), previous.bookSize);
    write(globalPath, previous.global.data(), previous.globalSize);
    const uint8_t progress[] = {12, 34, 56, 78};
    write(FIXTURE, progress, sizeof(progress));
    for (unsigned slot = 0; slot < 64; ++slot) {
      const auto& stored = slot == 0 ? deferredRecord : record;
      const size_t storedSize = slot == 0 ? deferredSize : size;
      const auto created = cj::createRecord(slot, stored, storedSize);
      if (created != cj::CreateResult::Created) {
        char path[64];
        snprintf(path, sizeof(path), "/.completion-journal/.completion-%02u", slot);
        std::fprintf(stderr, "rollover create slot %u: %d; lookup=%d\n", slot, static_cast<int>(created),
                     static_cast<int>(Storage.lookupPath(path)));
      }
      assert(created == cj::CreateResult::Created);
      assert(cj::createSeal(slot, stored, storedSize) == cj::CreateResult::Created);
      cj::Seal receipt{};
      assert(cj::encodeRetirement(slot, stored, storedSize, receipt));
      char path[64];
      snprintf(path, sizeof(path), "/.completion-journal/.cretire-%02u", slot);
      write(path, receipt.data(), receipt.size());
    }
  }
  int failures = 0;
  for (int fault = -1; fault < 320; ++fault) {
    Disk disk;
    disk.sectors = baseline.sectors;
    cj::StartResult result;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      disk.failAt = fault;
      disk.failStop = failStop;
      result = cj::start(record, size);
    }
    if (fault >= 0 && !disk.injected) break;
    if (disk.injected) ++failures;
    if (fault == -1) {
      assert(result == cj::StartResult::Started);
      std::printf("%s %s rollover baseline writes: %d\n", exfat ? "exFAT" : "FAT", failStop ? "fail-stop" : "transient",
                  disk.writes);
    }
    disk.dead = false;
    disk.failAt = -1;
    FsVolume volume;
    assert(volume.begin(&disk));
    Storage.attach(volume);
    const auto check = [](const char* path, const uint8_t* expected, size_t length) {
      auto file = Storage.open(path, O_RDONLY);
      assert(file && file.fileSize() == length);
      std::array<uint8_t, cj::GLOBAL_BYTES> bytes{};
      assert(file.read(bytes.data(), length) == static_cast<int>(length) && file.close());
      assert(std::memcmp(bytes.data(), expected, length) == 0);
    };
    check(bookPath, previous.book.data(), previous.bookSize);
    check(globalPath, previous.global.data(), previous.globalSize);
    const uint8_t progress[] = {12, 34, 56, 78};
    check(FIXTURE, progress, sizeof(progress));
    if (result == cj::StartResult::Started) {
      cj::Record found{};
      size_t foundSize = 0;
      unsigned slot = 99;
      assert(cj::scanRecords(found, foundSize, slot) == cj::ScanResult::Candidate);
      assert(slot == 0 && foundSize == size && found == record);
      if (fault == -1) {
        cj::Seal retired{};
        assert(cj::encodeRetirement(0, record, size, retired));
        auto file = Storage.open("/.completion-journal/.cretire-00", O_CREAT | O_EXCL | O_WRONLY);
        assert(file && file.write(retired.data(), retired.size()) == retired.size() && file.sync() && file.close());
        assert(cj::deferredRetirementProof(deferredSource, cj::checksum(deferredRecord.data(), deferredSize)) ==
               cj::DeferredRetirementProof::Retired);
      }
    }
  }
  assert(failures > 0);
  std::printf("%s %s rollover: %d injected writes preserved stats and progress\n", exfat ? "exFAT" : "FAT",
              failStop ? "fail-stop" : "transient", failures);
}

int main() {
  for (bool exfat : {false, true})
    for (bool failStop : {false, true}) {
#if defined(SDFAT_IMPORT_SNAPSHOT)
      importSnapshotFaults(exfat, failStop);
#endif
#if defined(SDFAT_IMPORT_TRANSACTION)
      importTransactionFaults(exfat, failStop);
      importRecoveryFaults(exfat, failStop);
#endif
      if (exfat && !failStop) {
        exfatFailedSyncRetainsMetadata(false);
        exfatFailedSyncRetainsMetadata(true);
      }
      if (!failStop) {
        verifyMovedFilesOnRealFilesystem(exfat);
#if defined(SDFAT_REAL_MOVE_STORES)
        verifyMovedFilesOnRealFilesystem(exfat, true);
#endif
        deferredSourceRemount(exfat);
        retiredIntentRecovery(exfat);
        emptyMoveCopies(exfat);
      }
      rolloverFaults(exfat, failStop);
      deferredPublishFaults(exfat, failStop);
      moveReferenceWriteFaults(exfat, failStop);
      exercise(exfat, failStop);
      replayFaults(exfat, failStop);
      moveFileFaults(exfat, failStop, true);
      moveFileFaults(exfat, failStop, false);
#if defined(SDFAT_CLEANUP_EXPERIMENT)
      cleanupFaults(exfat, failStop);
#endif
#if defined(SDFAT_ALIAS_LARGE)
      largeAliasFixture(exfat, failStop, false);
      largeAliasFixture(exfat, failStop, true);
#if defined(SDFAT_ALIAS_ONE_SECTOR)
      if (!exfat) largeAliasFixture(false, failStop, false, true);
#endif
#endif
    }
}
