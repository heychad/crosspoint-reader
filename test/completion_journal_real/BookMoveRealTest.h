#pragma once

#include <vector>

#include "util/BookMoveStorage.h"

namespace bm = bookmove;
namespace bookmove {
inline bool testDestinationClear = true;
inline bool testReferencesWritable = true;
inline unsigned testReferenceWrites = 0;
#if !defined(SDFAT_BOOK_MOVE_REFERENCES)
bool destinationReferencesClear(const Work&) { return testDestinationClear; }
bool updateReferences(const Work&) {
  ++testReferenceWrites;
  return testReferencesWritable;
}
#endif
}  // namespace bookmove

namespace bookmovetest {
const std::string from = "/Incoming/Author - Book.epub";
const std::string to = "/Books/Genre/Book - Author.epub";

std::vector<uint8_t> bookmark(const std::string& book) {
  std::vector<uint8_t> bytes{5, 1, 0};
  const auto string = [&](const std::string& value) {
    for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(value.size() >> (i * 8)));
    bytes.insert(bytes.end(), value.begin(), value.end());
  };
  string("A title");
  string("An author");
  string(book);
  bytes.resize(bytes.size() + 58 + 2 + 64, 0x42);
  bytes.push_back(0x7f);
  return bytes;
}

void write(const std::string& path, const std::vector<uint8_t>& bytes) {
  const auto parent = path.substr(0, path.find_last_of('/'));
  if (!parent.empty()) assert(Storage.mkdir(parent.c_str(), true) || Storage.exists(parent.c_str()));
  auto file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL);
  assert(file && file.write(bytes.data(), bytes.size()) == bytes.size() && file.sync() && file.close());
}

std::vector<uint8_t> read(const std::string& path) {
  auto file = Storage.open(path.c_str());
  assert(file && !file.isDirectory());
  std::vector<uint8_t> bytes(file.fileSize());
  assert(file.read(bytes.data(), bytes.size()) == static_cast<int>(bytes.size()) && file.close());
  return bytes;
}

std::vector<uint8_t> contents(size_t index) { return {static_cast<uint8_t>(index), 0, 1, 0xff, 0x88, 0x55}; }

void seed(FsVolume& volume, bool exfat, Disk& disk) {
  std::array<uint8_t, 512> scratch{};
  if (exfat) {
    ExFatFormatter formatter;
    assert(formatter.format(&disk, scratch.data()));
  } else {
    FatFormatter formatter;
    assert(formatter.format(&disk, scratch.data()));
  }
  assert(volume.begin(&disk));
  Storage.attach(volume);
  assert(Storage.mkdir("/Books/Genre", true));
  for (size_t i = 0; i < bm::ENTRY_COUNT; ++i) {
    std::string source, target;
    assert(bm::itemPaths(from, to, bm::BookKind::Reflowable, i, source, target));
    if (bm::ENTRIES[i].kind == bm::EntryKind::Directory) {
      write(source + "/progress.bin", contents(i));
      write(source + "/progress.bin.bak", contents(i + 1));
      write(source + "/stats_v5.bin", contents(i + 2));
      write(source + "/reader_prefs.json", contents(i + 3));
      write(source + "/unknown/recovery.part", contents(i + 4));
      if (i == 21 || i == 22) {
        write(source + "/sections/page.bin", contents(i + 5));
        write(source + "/css_rules.cache", contents(i + 6));
      }
    } else {
      write(source, bm::ENTRIES[i].kind == bm::EntryKind::LegacyBookmark ? bookmark(from) : contents(i));
    }
  }
  bm::testDestinationClear = true;
  bm::testReferencesWritable = true;
  bm::testReferenceWrites = 0;
}

void verify(const bm::Work& work) {
  assert(!bm::pending());
  for (size_t i = 0; i < bm::ENTRY_COUNT; ++i) {
    std::string source, target;
    assert(bm::itemPaths(from, to, bm::BookKind::Reflowable, i, source, target));
    assert(Storage.lookupPath(source.c_str()) == HalStorage::PathStatus::Missing);
    if (bm::ENTRIES[i].kind == bm::EntryKind::Directory) {
      assert(read(target + "/progress.bin") == contents(i));
      assert(read(target + "/progress.bin.bak") == contents(i + 1));
      assert(read(target + "/stats_v5.bin") == contents(i + 2));
      assert(read(target + "/reader_prefs.json") == contents(i + 3));
      assert(read(target + "/unknown/recovery.part") == contents(i + 4));
      uint32_t cluster = 0;
      assert(Storage.identifyMoveDirectory(target.c_str(), cluster) == HalStorage::DirectoryMoveStatus::Ready);
      assert(cluster == work.identities[i].firstCluster);
      if (i == 21 || i == 22) {
        assert(Storage.lookupPath((target + "/sections").c_str()) == HalStorage::PathStatus::Missing);
        assert(Storage.lookupPath((target + "/css_rules.cache").c_str()) == HalStorage::PathStatus::Missing);
      }
    } else if (bm::ENTRIES[i].kind == bm::EntryKind::LegacyBookmark) {
      assert(read(target) == bookmark(to));
      assert(read(work.archive + "/bookmark_" + std::to_string(i)) == bookmark(from));
    } else {
      assert(read(target) == contents(i));
    }
  }
}

void normal(bool exfat) {
  Disk disk;
  FsVolume volume;
  seed(volume, exfat, disk);
  disk.writes = 0;
  bm::Work work;
  work.from = from;
  work.to = to;
  assert(bm::prepare(work) == bm::Result::Moved);
  assert(bm::pending());
  // Simulate a persisted-reference failure after every file has moved.
  bm::testReferencesWritable = false;
  assert(bm::execute(work) == bm::Result::RecoveryRequired);
  assert(bm::pending());
  bm::Work recovered;
  assert(bm::inspect(recovered) == bm::Result::Moved);
  bm::testReferencesWritable = true;
  assert(bm::execute(recovered) == bm::Result::Moved);
  verify(recovered);
  assert(bm::testReferenceWrites == 2);
  std::printf("Transaction/retry writes: %d; work bytes: %zu\n", disk.writes, sizeof(bm::Work));
  std::printf("%s book moves: complete state, bookmark rewrites, references retry, archive passed\n",
              exfat ? "exFAT" : "FAT");
}

void conflicts(bool exfat) {
  for (unsigned conflict = 0; conflict < 8; ++conflict) {
    Disk disk;
    FsVolume volume;
    seed(volume, exfat, disk);
    bm::Work work;
    work.from = from;
    work.to = to;
    if (conflict == 0) write(to, {9, 8, 7});
    if (conflict == 1) write(bm::rootPath(to, bm::BookKind::Reflowable, bm::Root::Statistics) + "/other.bin", {1});
    if (conflict == 2) bm::testDestinationClear = false;
    if (conflict == 3) {
      const auto path = bm::rootPath(from, bm::BookKind::Reflowable, bm::Root::CrcBookmarks) + ".bookmark-edit.guard";
      assert(Storage.remove(path.c_str()));
      write(path, {9, 8, 7});
    }
    if (conflict >= 4) {
      const auto root = conflict < 6 ? bm::Root::CrcBookmarks : bm::Root::HashBookmarks;
      const auto& book = conflict % 2 == 0 ? from : to;
      write(bm::rootPath(book, bm::BookKind::Reflowable, root) + ".bookmark-edit.tmp", {1});
    }
    const auto before = disk.sectors;
    assert(bm::prepare(work) == bm::Result::Conflict);
    assert(disk.sectors == before);
    assert(!bm::pending());
    assert(read(from) == contents(0));
  }
}

void faults(bool exfat) {
  Disk baseline;
  {
    FsVolume volume;
    seed(volume, exfat, baseline);
  }
  unsigned injected = 0, recovered = 0, blocked = 0;
  // The original book data sector is immutable throughout every fault/retry.
  std::vector<Sector_t> bookSectors;
  const auto bookBytes = contents(0);
  for (const auto& [sector, bytes] : baseline.sectors)
    if (std::equal(bookBytes.begin(), bookBytes.end(), bytes.begin())) bookSectors.push_back(sector);
  assert(!bookSectors.empty());
  bool terminal = false;
  for (int failAt = 0; failAt < 1200; ++failAt) {
    Disk disk = baseline;
    bm::Work work;
    work.from = from;
    work.to = to;
    bool reached = false;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      bm::testReferencesWritable = true;
      bm::testDestinationClear = true;
      disk.writes = 0;
      disk.failAt = failAt;
      disk.failStop = true;
      const auto prepared = bm::prepare(work);
      const auto result = prepared == bm::Result::Moved ? bm::execute(work) : prepared;
      reached = disk.injected;
      if (!reached) {
        assert(result == bm::Result::Moved);
        verify(work);
      }
    }
    for (auto sector : bookSectors) assert(disk.sectors.at(sector) == baseline.sectors.at(sector));
    if (!reached) {
      terminal = true;
      break;
    }
    ++injected;
    disk.dead = false;
    disk.failAt = -1;
    disk.failStop = false;
    {
      FsVolume volume;
      assert(volume.begin(&disk));
      Storage.attach(volume);
      bm::Work retry;
      const auto observed = bm::inspect(retry);
      if (observed == bm::Result::Moved && bm::execute(retry) == bm::Result::Moved) {
        verify(retry);
        ++recovered;
      } else {
        ++blocked;
        // A conflicting or unsealed journal cannot authorize further changes.
        const auto before = disk.sectors;
        if (observed != bm::Result::Moved) assert(bm::inspect(retry) != bm::Result::Moved);
        assert(disk.sectors == before);
      }
    }
    for (auto sector : bookSectors) assert(disk.sectors.at(sector) == baseline.sectors.at(sector));
  }
  assert(terminal && injected && recovered && blocked);
  std::printf("%s book moves: %u fail-stop write cuts, %u recovered, %u preserved for recovery\n",
              exfat ? "exFAT" : "FAT", injected, recovered, blocked);
}
}  // namespace bookmovetest

#if defined(SDFAT_BOOK_MOVE_REFERENCES)
#include "BookMoveReferencesRealTest.h"
#endif
int bookMoveTests() {
#if defined(SDFAT_BOOK_MOVE_REFERENCES)
  return bookMoveReferenceTests();
#endif
  for (bool exfat : {false, true}) {
    bookmovetest::normal(exfat);
    bookmovetest::conflicts(exfat);
    bookmovetest::faults(exfat);
  }
  return 0;
}
