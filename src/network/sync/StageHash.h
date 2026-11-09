#pragma once

#include <mbedtls/sha256.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ExclusiveStage.h"

namespace wirecheck {
// A process-local snapshot of a stage. The caller owns the buffer, polling,
// cancellation, identity check, and any later publication decision.
class StageHash {
 public:
  enum class State { Idle, Reading, Complete, Failed };

  StageHash(HalStorage& storage, uint8_t* buffer, size_t capacity)
      : storage(storage), buffer(buffer), capacity(capacity) {
    mbedtls_sha256_init(&hash);
  }
  ~StageHash() { mbedtls_sha256_free(&hash); }
  StageHash(const StageHash&) = delete;
  StageHash& operator=(const StageHash&) = delete;

  bool begin(const char* name, uint64_t size) {
    if (state_ != State::Idle || !buffer || !capacity || capacity > 4096 || size > MAX_SIZE ||
        !ExclusiveStage::validName(name))
      return fail();
    path[0] = '/';
    std::memcpy(path + 1, name, 46);
    expected = size;
    if (mbedtls_sha256_starts(&hash, 0) != 0 || !storage.withStorageLock(startUnderLock, this)) return fail();
    state_ = State::Reading;
    return true;
  }

  State step() {
    if (state_ != State::Reading) return state_;
    if (position < expected) {
      count = static_cast<size_t>(std::min<uint64_t>(capacity, expected - position));
      if (!storage.withStorageLock(readUnderLock, this) || mbedtls_sha256_update(&hash, buffer, count) != 0) {
        fail();
        return state_;
      }
      position += count;
    }
    if (position == expected) {
      if (mbedtls_sha256_finish(&hash, digest) != 0 || !storage.withStorageLock(checkUnderLock, this)) {
        fail();
        return state_;
      }
      state_ = State::Complete;
    }
    return state_;
  }

  void cancel() {
    if (state_ != State::Complete) fail();
  }
  State state() const { return state_; }
  bool copyDigest(uint8_t out[32]) {
    if (state_ != State::Complete || !out) return false;
    if (!storage.withStorageLock(checkUnderLock, this)) return fail();
    std::memcpy(out, digest, sizeof(digest));
    return true;
  }

 private:
  static constexpr uint64_t MAX_SIZE = 0x7fffffffULL - 1024;
  static bool startUnderLock(void* context) {
    auto& self = *static_cast<StageHash*>(context);
    if (!self.storage.beginMutationSnapshot(self.snapshot)) return false;
    auto file = self.storage.open(self.path, O_RDONLY);
    if (!file) return false;
    const bool correct = !file.isDirectory() && file.fileSize64() == self.expected &&
                         file.captureReadCursor(self.cursor) && self.cursor.position == 0;
    const bool closed = file.close();
    return correct && closed && self.storage.mutationSnapshotUnchanged(self.snapshot);
  }
  static bool readUnderLock(void* context) {
    auto& self = *static_cast<StageHash*>(context);
    if (!self.storage.mutationSnapshotUnchanged(self.snapshot)) return false;
    auto file = self.storage.open(self.path, O_RDONLY);
    if (!file) return false;
    const bool correct = !file.isDirectory() && file.fileSize64() == self.expected &&
                         self.cursor.position == self.position && file.restoreReadCursor(self.cursor) &&
                         file.read(self.buffer, self.count) == static_cast<int>(self.count) &&
                         file.captureReadCursor(self.cursor) && self.cursor.position == self.position + self.count;
    const bool closed = file.close();
    return correct && closed && self.storage.mutationSnapshotUnchanged(self.snapshot);
  }
  static bool checkUnderLock(void* context) {
    auto& self = *static_cast<StageHash*>(context);
    return self.storage.mutationSnapshotUnchanged(self.snapshot);
  }
  bool fail() {
    state_ = State::Failed;
    return false;
  }

  HalStorage& storage;
  uint8_t* buffer;
  size_t capacity;
  mbedtls_sha256_context hash;
  HalStorage::MutationSnapshot snapshot;
  HalFile::ReadCursor cursor;
  char path[47]{};
  uint8_t digest[32]{};
  uint64_t expected = 0;
  uint64_t position = 0;
  size_t count = 0;
  State state_ = State::Idle;
};
}  // namespace wirecheck
