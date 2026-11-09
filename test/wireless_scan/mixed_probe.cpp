#include "CooperativeDirectoryScan.h"
#include "DirectoryEnumerator.h"
#include "DirectoryValidator.h"
#if TEST_EXFAT
#include <ExFatLib/ExFatFile.h>
#include <ExFatLib/ExFatFormatter.h>
#include <ExFatLib/ExFatVolume.h>
using Formatter = ExFatFormatter;
using Volume = ExFatVolume;
using File = ExFatFile;
constexpr auto FORMAT = wirecheck::Format::ExFat;
#else
#include <FatLib/FatFile.h>
#include <FatLib/FatFormatter.h>
#include <FatLib/FatVolume.h>
using Formatter = FatFormatter;
using Volume = FatVolume;
using File = FatFile;
constexpr auto FORMAT = wirecheck::Format::Fat;
#endif
#include <common/FsBlockDeviceInterface.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

struct Disk : FsBlockDeviceInterface {
  std::unordered_map<Sector_t, std::array<uint8_t, 512>> sectors;
  int reads = 0, fail = -1;
  bool injected = false;
  bool isBusy() override { return false; }
  Sector_t sectorCount() override { return 1024 * 1024; }
  bool syncDevice() override { return true; }
  bool readSector(Sector_t s, uint8_t* out) override {
    assert(s < sectorCount());
    if (reads++ == fail) {
      injected = true;
      return false;
    }
    auto it = sectors.find(s);
    if (it == sectors.end())
      std::memset(out, 0, 512);
    else
      std::memcpy(out, it->second.data(), 512);
    return true;
  }
  bool readSectors(Sector_t s, uint8_t* out, size_t n) override {
    for (size_t i = 0; i < n; ++i)
      if (!readSector(s + i, out + i * 512)) return false;
    return true;
  }
  bool writeSector(Sector_t s, const uint8_t* in) override {
    assert(s < sectorCount());
    std::memcpy(sectors[s].data(), in, 512);
    return true;
  }
  bool writeSectors(Sector_t s, const uint8_t* in, size_t n) override {
    for (size_t i = 0; i < n; ++i)
      if (!writeSector(s + i, in + i * 512)) return false;
    return true;
  }
};
struct LiveScan {
  File* directory;
  unsigned steps = 0, emitted = 0;
  int cancelAt = -1, changeAt = -1;
  bool changed = false, closed = false;
  static wirecheck::ReadResult read(void* raw, uint8_t* out) {
    auto& s = *static_cast<LiveScan*>(raw);
    if (static_cast<int>(s.steps) == s.changeAt) {
      s.changed = true;
      return wirecheck::ReadResult::Changed;
    }
    const auto n = s.directory->read(out, 32);
    ++s.steps;
    if (n < 0) return wirecheck::ReadResult::Error;
    if (n == 0) return wirecheck::ReadResult::End;
    return n == 32 ? wirecheck::ReadResult::Record : wirecheck::ReadResult::Error;
  }
  static bool valid(void* raw) { return !static_cast<LiveScan*>(raw)->changed; }
  static bool tick(void* raw) {
    auto& s = *static_cast<LiveScan*>(raw);
    return static_cast<int>(s.steps) != s.cancelAt;
  }
  static bool emit(void* raw, const wirecheck::DirectoryEntry&) {
    ++static_cast<LiveScan*>(raw)->emitted;
    return true;
  }
  static bool close(void* raw) {
    auto& s = *static_cast<LiveScan*>(raw);
    s.closed = true;
    return s.directory->close();
  }
  wirecheck::ScanOps ops() { return {this, read, valid, tick, emit, close}; }
};

int main() {
  Disk disk;
  uint8_t scratch[512];
  Formatter formatter;
  assert(formatter.format(&disk, scratch));
  Volume volume;
  assert(volume.begin(&disk));
  File root;
  assert(root.openRoot(&volume));
  File folder;
  assert(folder.mkdir(&root, "Mixed"));
  for (int i = 0; i < 360; ++i) {
    File file;
    std::string name;
    if (i % 3 == 0) {
      char shortName[13];
      std::snprintf(shortName, sizeof(shortName), "F%07d.TXT", i);
      name = shortName;
    } else if (i % 3 == 1)
      name = std::to_string(i) + " reading Café 世界 📚.epub";
    else
      name = std::to_string(i) + " folder";
    if (i % 3 == 2) {
      assert(file.mkdir(&folder, name.c_str()));
    } else {
      assert(file.open(&folder, name.c_str(), O_CREAT | O_WRONLY));
      for (int j = 0; j < i % 31; ++j) assert(file.write(static_cast<uint8_t>(j)) == 1);
    }
    assert(file.close());
  }
  // Produce deleted slots that the record scanner must advance through.
  for (int i = 0; i < 360; i += 6) {
    char shortName[13];
    std::snprintf(shortName, sizeof(shortName), "F%07d.TXT", i);
    File file;
    assert(file.open(&folder, shortName, O_RDWR));
    assert(file.remove());
  }
  assert(folder.close());
  assert(root.close());
  // SdFat reports these known no-ops as false, which must not poison future
  // inventory snapshots when their preconditions are verified under lock.
  assert(!volume.mkdir("/Mixed/2 folder"));
  assert(!volume.remove("/Mixed/absent-entry"));
  assert(volume.cacheClear());
  assert(root.openRoot(&volume));
  assert(folder.open(&root, "Mixed", O_RDONLY));
  // Pinned SdFat normalizes these path spellings to the existing short file.
  for (const char* alias : {" F0000003.TXT", "F0000003.TXT.", "F0000003.TXT "}) {
    File normalized;
    assert(normalized.open(&folder, alias, O_RDONLY));
    char actualName[64]{};
    assert(normalized.getName(actualName, sizeof(actualName)) > 0);
    assert(std::string(actualName) == "F0000003.TXT");
    assert(normalized.close());
  }
  folder.rewind();
  struct Item {
    std::string name;
    uint64_t size;
    bool directory;
  };
  std::vector<Item> expected, actual;
  while (true) {
    File entry;
    if (!entry.openNext(&folder, O_RDONLY)) {
      assert(!folder.getError());
      break;
    }
    char name[766]{};
    assert(entry.getName(name, sizeof(name)) > 0);
    const bool directory = entry.isDir();
    const auto size = directory ? 0 : entry.fileSize();
    expected.push_back({name, size, directory});
    assert(entry.close());
  }
  assert(expected.size() == 300);
  folder.rewind();
  wirecheck::DirectoryEnumerator scanner(FORMAT);
  wirecheck::DirectoryEntry output;
  int records = 0;
  while (true) {
    std::array<uint8_t, 32> item{};
    const int n = folder.read(item.data(), item.size());
    assert(n >= 0);
    if (n == 0) {
      assert(scanner.finish() == wirecheck::Step::End);
      break;
    }
    const auto step = scanner.accept(item.data(), n, output);
    ++records;
    assert(step != wirecheck::Step::Error);
    if (step == wirecheck::Step::End) break;
    if (step == wirecheck::Step::Entry) actual.push_back({output.name, output.size, output.directory});
  }
  assert(expected.size() == actual.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    if (expected[i].name != actual[i].name || expected[i].size != actual[i].size ||
        expected[i].directory != actual[i].directory) {
      std::fprintf(stderr, "mismatch %zu expected %s actual %s\n", i, expected[i].name.c_str(), actual[i].name.c_str());
      return 2;
    }
  }
  assert(folder.close());
  assert(root.close());
  std::printf("mixed_entries=%zu records=%d deleted_files=60 sizes_and_types_match=yes\n", actual.size(), records);
  // Re-open and re-read under every single-sector read fault. Partial entries
  // may be accumulated, but no failed scan may be reported complete.
  int baseline = 0, caught = 0;
  for (int failure = -1; failure < 512; ++failure) {
    disk.fail = -1;
    assert(volume.cacheClear());
    disk.reads = 0;
    disk.injected = false;
    disk.fail = failure;
    File parent, dir;
    bool ok = parent.openRoot(&volume) && dir.open(&parent, "Mixed", O_RDONLY);
    wirecheck::DirectoryEnumerator bounded(FORMAT);
    wirecheck::DirectoryEntry item;
    unsigned seen = 0;
    while (ok) {
      std::array<uint8_t, 32> raw{};
      const int n = dir.read(raw.data(), raw.size());
      if (n < 0) {
        ok = false;
        break;
      }
      if (n == 0) {
        ok = bounded.finish() == wirecheck::Step::End;
        break;
      }
      const auto step = bounded.accept(raw.data(), static_cast<size_t>(n), item);
      if (step == wirecheck::Step::Error) {
        ok = false;
        break;
      }
      if (step == wirecheck::Step::End) break;
      if (step == wirecheck::Step::Entry) {
        if (seen >= expected.size() || expected[seen].name != item.name || expected[seen].size != item.size ||
            expected[seen].directory != item.directory) {
          ok = false;
          break;
        }
        ++seen;
      }
    }
    if (dir) ok = dir.close() && ok;
    if (parent) ok = parent.close() && ok;
    ok = ok && seen == expected.size();
    if (failure < 0) {
      assert(ok);
      baseline = disk.reads;
    }
    if (disk.injected) {
      ++caught;
      if (ok) {
        std::fprintf(stderr, "read fault reported complete: %d\n", failure);
        return 3;
      }
    }
    if (failure > baseline + 4) break;
  }
  assert(caught > 0);
  std::printf("mixed_read_failures_detected=%d\n", caught);
  for (int mode = 0; mode < 6; ++mode) {
    disk.fail = -1;
    assert(volume.cacheClear());
    File parent, dir;
    assert(parent.openRoot(&volume));
    assert(dir.open(&parent, "Mixed", O_RDONLY));
    LiveScan context{&dir};
    if (mode == 1) context.cancelAt = 0;
    if (mode == 2) context.cancelAt = 200;
    if (mode == 3) context.changeAt = 0;
    if (mode == 4) context.changeAt = 200;
    wirecheck::CooperativeDirectoryScan scanner(FORMAT, context.ops());
    wirecheck::ScanResult result = wirecheck::ScanResult::Pending;
    bool changedDisk = false;
    while (result == wirecheck::ScanResult::Pending) {
      if (mode == 5 && context.steps == 200 && !changedDisk) {
        // Model the HAL's mutation notification before modifying the image.
        context.changed = true;
        File mutator, newBook;
        assert(mutator.open(&parent, "Mixed", O_RDONLY));
        assert(newBook.open(&mutator, "NEWBOOK.TXT", O_CREAT | O_WRONLY));
        assert(newBook.close());
        assert(mutator.close());
        changedDisk = true;
      }
      result = scanner.step();
    }
    assert(context.closed && parent.close());
    if (mode == 0) {
      assert(result == wirecheck::ScanResult::Complete && context.emitted == 300);
    } else {
      assert(result == wirecheck::ScanResult::Failed);
      if (mode == 5) assert(changedDisk);
    }
  }
  std::printf("cooperative_scan_cancellation_and_mutation=pass\n");
}
