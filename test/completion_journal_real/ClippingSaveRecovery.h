#pragma once

#include <ClippingStore.h>

namespace clippingrecoverytest {
inline std::string text(unsigned index) { return std::string(117 + index * 25, static_cast<char>('A' + index)); }

inline bool append(ClippingStore& store, unsigned index) {
  Clipping entry;
  snprintf(entry.id, sizeof(entry.id), "clip-%04u", index);
  const auto value = text(index);
  entry.textLength = static_cast<uint16_t>(value.size());
  entry.syncRevision = index + 1;
  return store.applyRemote(entry, value, false);
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
