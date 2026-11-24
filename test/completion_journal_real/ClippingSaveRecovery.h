#pragma once

#include <ClippingStore.h>

#include <functional>
#include <vector>

namespace clippingrecoverytest {
inline std::string text(unsigned index) { return std::string(117 + (index % 16) * 25, static_cast<char>('A' + index)); }

inline bool append(ClippingStore& store, unsigned index) {
  Clipping entry;
  snprintf(entry.id, sizeof(entry.id), "clip-%04u", index);
  const auto value = text(index);
  entry.textLength = static_cast<uint16_t>(value.size());
  entry.syncRevision = index + 1;
  return store.applyRemote(entry, value, false);
}

// Give the writer three valid, distinct files near the maximum X4 Pro size.
// Interleaved allocations exercise fragmented chains on the same volume.
inline void largeStore() {
  constexpr char book[] = "/large-clippings.epub";
  const std::string path = "/.crosspoint/clippings/epub_" + std::to_string(std::hash<std::string>{}(book)) + ".bin";
  const std::string passage(CLIPPING_TEXT_MAX, 'x');
  ClippingStore store;
  assert(store.loadForBook(book, "Large", "Author", "epub"));
  assert(store.addClipping(0, 1, 1, 100, 1, 2, 2, "Chapter", 0, passage, 1) == ClippingStore::AddResult::Added);
  const size_t headerSize = store.clippingAt(0)->textOffset - 156;
  store.unload();
  auto sample = Storage.open(path.c_str());
  std::vector<uint8_t> bytes(sample.size());
  assert(sample.read(bytes.data(), bytes.size()) == static_cast<int>(bytes.size()) && sample.close());
  const uint16_t count = CLIPPING_MAX_PER_BOOK - 1;
  static_assert(CLIPPING_MAX_PER_BOOK == 1024);
  memcpy(bytes.data() + 1, &count, sizeof(count));
  {
    std::array<HalFile, 3> files;
    size_t index = 0;
    for (const char* suffix : {"", ".bak", ".tmp"}) {
      files[index] = Storage.open((path + suffix).c_str(), O_CREAT | O_TRUNC | O_WRONLY);
      assert(files[index] && files[index].write(bytes.data(), headerSize) == headerSize);
      ++index;
    }
    for (uint16_t row = 0; row < count; ++row) {
      for (auto& file : files) {
        assert(file.write(bytes.data() + headerSize, bytes.size() - headerSize) == bytes.size() - headerSize);
      }
    }
    for (auto& file : files) assert(file.sync() && file.close());
  }
  assert(!Storage.distinctClippingChains(path.c_str(), path.c_str()));
  assert(Storage.distinctClippingChains(path.c_str(), (path + ".tmp").c_str()));
  assert(store.loadForBook(book, "Large", "Author", "epub"));
  assert(append(store, count));
  store.unload();
  assert(store.loadForBook(book, "Large", "Author", "epub"));
  assert(store.clippingCount() == CLIPPING_MAX_PER_BOOK);
  for (size_t row = 0; row < store.clippingCount(); ++row) {
    std::string actual;
    assert(store.readClippingText(row, actual));
    assert(actual == (row == count ? text(count) : passage));
  }
}

// Fail each individual sector operation of a real SdFat save and reopen the book.
inline void run(bool exfat) {
  Disk baseline;
  uint8_t sector[512];
  if (exfat) {
    ExFatFormatter format;
    assert(format.format(&baseline, sector));
  } else {
    FatFormatter format;
    assert(format.format(&baseline, sector));
  }
  FsVolume volume;
  assert(volume.begin(&baseline));
  Storage.attach(volume);
  constexpr char book[] = "/clipping-test.epub";
  {
    ClippingStore store;
    assert(store.loadForBook(book, "Book", "Author", "epub"));
    for (unsigned i = 0; i < 3; ++i) assert(append(store, i));
  }
  volume.end();
  {
    Disk largeDisk = baseline;
    assert(volume.begin(&largeDisk));
    Storage.attach(volume);
    largeStore();
    volume.end();
  }

  for (const bool failRead : {false, true}) {
    unsigned failures = 0;
    for (int operation = 0; operation < 512; ++operation) {
      Disk trial = baseline;
      assert(volume.begin(&trial));
      Storage.attach(volume);
      bool acknowledged;
      {
        ClippingStore store;
        assert(store.loadForBook(book, "Book", "Author", "epub"));
        trial.reads = trial.writes = 0;
        (failRead ? trial.failReadAt : trial.failAt) = operation;
        acknowledged = append(store, 3);
        trial.failReadAt = trial.failAt = -1;
      }
      volume.end();
      if (!trial.injected && !trial.injectedRead) break;
      ++failures;
      assert(volume.begin(&trial));
      Storage.attach(volume);
      ClippingStore recovered;
      assert(recovered.loadForBook(book, "Book", "Author", "epub"));
      assert(recovered.clippingCount() == 4 || (!acknowledged && recovered.clippingCount() == 3));
      for (size_t i = 0; i < recovered.clippingCount(); ++i) {
        std::string actual;
        assert(recovered.readClippingText(i, actual) && actual == text(i));
      }
      volume.end();
    }
    assert(failures > 0 && failures < 512);
    std::printf("%s clip save: prior text survived %u transient %s failures\n", exfat ? "exFAT" : "FAT", failures,
                failRead ? "read" : "write");
  }
}
}  // namespace clippingrecoverytest
