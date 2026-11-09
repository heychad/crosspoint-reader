#pragma once

#include <CheckedStoragePath.h>
#include <HalStorage.h>
#include <strings.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "PublicationIntentCodec.h"

namespace wirecheck::publication {
// Unconnected journal storage. The caller supplies MAX_RECORD scratch bytes
// off a small task stack and excludes untracked/raw writers. Scratch must not
// overlap input or output. A valid record does not authorize publication.
class IntentStore {
 public:
  enum class Load { Missing, Valid, Invalid, Unavailable };
  // Existing does not validate the record. Uncertain may leave a partial
  // record; neither result authorizes overwriting or retrying creation.
  enum class Prepare { Prepared, Existing, Invalid, Unavailable, Uncertain };
  static constexpr const char* PATH = "/.crosspoint/xsync-publish.intent";

  explicit IntentStore(HalStorage& storage) : storage_(storage) {}

  Load load(Intent& out, uint8_t* scratch, size_t capacity) {
    if (!scratch || capacity < MAX_RECORD) return Load::Unavailable;
    ReadRequest request{this, &out, scratch};
    if (!storage_.withStorageLock(readUnderLock, &request)) return Load::Unavailable;
    return request.result;
  }

  Prepare prepare(const View& view, uint8_t* scratch, size_t capacity) {
    if (!scratch || capacity < MAX_RECORD) return Prepare::Unavailable;
    if (!validV1(view) || !checkedDestination(view.destination, scratch, capacity)) return Prepare::Invalid;
    const size_t length = encodeV1(view, scratch, capacity);
    if (!length) return Prepare::Invalid;
    WriteRequest request{this, scratch, length};
    if (!storage_.withStorageLock(writeUnderLock, &request)) return Prepare::Unavailable;
    return request.result;
  }

 private:
  struct ReadRequest {
    IntentStore* self;
    Intent* out;
    uint8_t* scratch;
    Load result = Load::Unavailable;
  };
  struct WriteRequest {
    IntentStore* self;
    const uint8_t* scratch;
    size_t length;
    Prepare result = Prepare::Unavailable;
  };

  static bool checkedDestination(std::string_view path, uint8_t* scratch, size_t capacity) {
    size_t start = 1;
    while (start < path.size()) {
      const auto slash = path.find('/', start);
      const size_t end = slash == path.npos ? path.size() : slash;
      const size_t length = end - start;
      if (!length || length + 1 > capacity) return false;
      std::memcpy(scratch, path.data() + start, length);
      scratch[length] = 0;
      if (!validCheckedStorageComponent(reinterpret_cast<const char*>(scratch))) return false;
      if (slash == path.npos) return true;
      start = slash + 1;
    }
    return false;
  }
  static bool named(HalFile& file, const char* expected) {
    char name[64]{};
    return file.getName(name, sizeof(name)) > 0 && strcasecmp(name, expected) == 0;
  }
  static bool validParent(HalStorage& storage, const HalStorage::MutationSnapshot& snapshot) {
    auto parent = storage.open("/.crosspoint", O_RDONLY);
    if (!parent) return false;
    const bool correct = parent.isDirectory() && named(parent, ".crosspoint");
    return parent.close() && correct && storage.mutationSnapshotUnchanged(snapshot);
  }
  static bool readUnderLock(void* context) {
    auto& r = *static_cast<ReadRequest*>(context);
    auto& storage = r.self->storage_;
    HalStorage::MutationSnapshot snapshot;
    if (!storage.beginMutationSnapshot(snapshot)) return false;
    if (!validParent(storage, snapshot)) return false;
    const auto status = storage.lookupPath(PATH);
    if (!storage.mutationSnapshotUnchanged(snapshot)) return false;
    if (status == HalStorage::PathStatus::Missing) {
      r.result = Load::Missing;
      return true;
    }
    if (status != HalStorage::PathStatus::Present) return false;
    auto file = storage.open(PATH, O_RDONLY);
    if (!file) return false;
    const bool correctFile = !file.isDirectory() && named(file, "xsync-publish.intent");
    const uint64_t size = correctFile ? file.fileSize64() : 0;
    bool read = false;
    if (correctFile && size <= MAX_RECORD && size >= HEADER_LENGTH + 5)
      read = file.read(r.scratch, static_cast<size_t>(size)) == static_cast<int>(size);
    const bool closed = file.close();
    if (!closed || !storage.mutationSnapshotUnchanged(snapshot)) return false;
    if (!correctFile) return false;
    if (size > MAX_RECORD || size < HEADER_LENGTH + 5) {
      r.result = Load::Invalid;
      return true;
    }
    if (!read) return false;
    r.result = decodeV1(r.scratch, static_cast<size_t>(size), *r.out) ? Load::Valid : Load::Invalid;
    return true;
  }
  static bool writeUnderLock(void* context) {
    auto& r = *static_cast<WriteRequest*>(context);
    auto& storage = r.self->storage_;
    HalStorage::MutationSnapshot snapshot;
    if (!storage.beginMutationSnapshot(snapshot)) return false;
    const auto status = storage.lookupPath(PATH);
    if (!storage.mutationSnapshotUnchanged(snapshot)) return false;
    if (status == HalStorage::PathStatus::Present) {
      r.result = Prepare::Existing;
      return true;
    }
    if (status != HalStorage::PathStatus::Missing) return false;
    if (!validParent(storage, snapshot)) return false;
    auto file = storage.open(PATH, O_CREAT | O_EXCL | O_WRONLY);
    if (!file) {
      r.result = Prepare::Uncertain;
      return true;
    }
    const bool written = file.write(r.scratch, r.length) == r.length;
    const bool closed = file.close();
    if (!written || !closed || !storage.monitoredStorageHealthy()) {
      r.result = Prepare::Uncertain;
      return true;
    }
    HalStorage::MutationSnapshot verify;
    if (!storage.beginMutationSnapshot(verify)) {
      r.result = Prepare::Uncertain;
      return true;
    }
    auto check = storage.open(PATH, O_RDONLY);
    if (!check) {
      r.result = Prepare::Uncertain;
      return true;
    }
    bool equal = !check.isDirectory() && named(check, "xsync-publish.intent") && check.fileSize64() == r.length;
    uint8_t chunk[128];
    for (size_t offset = 0; equal && offset < r.length;) {
      const size_t count = std::min(sizeof(chunk), r.length - offset);
      equal = check.read(chunk, count) == static_cast<int>(count) && std::memcmp(chunk, r.scratch + offset, count) == 0;
      offset += count;
    }
    const bool checkClosed = check.close();
    r.result =
        equal && checkClosed && storage.mutationSnapshotUnchanged(verify) ? Prepare::Prepared : Prepare::Uncertain;
    return true;
  }
  HalStorage& storage_;
};
}  // namespace wirecheck::publication
