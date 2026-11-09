#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "HalStorage.h"

namespace wirecheck {
// A root-only, create-exclusive staging file. Failures preserve partial data
// for inspection; no existing file is removed or replaced by this class. It
// detects other HAL mutations but does not reserve the staging path. No file
// handle stays open between operations.
class ExclusiveStage {
 public:
  enum class State { Idle, Writing, Complete, Failed };
  explicit ExclusiveStage(HalStorage& storage) : storage_(storage) {}
  ExclusiveStage(const ExclusiveStage&) = delete;
  ExclusiveStage& operator=(const ExclusiveStage&) = delete;

  bool begin(const char* name, uint64_t expectedSize) {
    if (state_ != State::Idle || !validName(name) || expectedSize > MAX_SIZE) return false;
    path_[0] = '/';
    std::memcpy(path_ + 1, name, NAME_LENGTH + 1);
    expected_ = expectedSize;
    if (!storage_.withStorageLock(createUnderLock, this)) {
      state_ = State::Failed;
      return false;
    }
    state_ = State::Writing;
    return true;
  }

  bool append(const uint8_t* data, size_t length) {
    if (state_ != State::Writing || (!data && length) || length > expected_ - written_) return fail();
    WriteRequest request{this, data, length};
    return storage_.withStorageLock(writeUnderLock, &request) || fail();
  }

  bool finish() {
    if (state_ != State::Writing || written_ != expected_) return fail();
    if (!storage_.withStorageLock(finishUnderLock, this)) return fail();
    state_ = State::Complete;
    return true;
  }
  State state() const { return state_; }
  uint64_t written() const { return written_; }

  static bool validName(const char* name) {
    if (!name || strnlen(name, NAME_LENGTH + 1) != NAME_LENGTH || std::memcmp(name, "__xsync_", 8) != 0 ||
        std::memcmp(name + 40, ".part", 5) != 0)
      return false;
    for (size_t i = 8; i < 40; ++i)
      if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f'))) return false;
    return true;
  }

 private:
  static constexpr size_t NAME_LENGTH = 45;
  static constexpr uint64_t MAX_SIZE = 0x7fffffffULL - 1024;
  struct WriteRequest {
    ExclusiveStage* self;
    const uint8_t* data;
    size_t length;
  };
  static bool createUnderLock(void* context) {
    auto& self = *static_cast<ExclusiveStage*>(context);
    if (self.storage_.lookupPath(self.path_) != HalStorage::PathStatus::Missing ||
        !self.storage_.monitoredStorageHealthy())
      return false;
    auto file = self.storage_.open(self.path_, O_CREAT | O_EXCL | O_WRONLY);
    if (!file) return false;
    const bool closed = file.close();
    return closed && self.storage_.captureMutationMarker(self.marker_);
  }
  static bool writeUnderLock(void* context) {
    auto& request = *static_cast<WriteRequest*>(context);
    auto& self = *request.self;
    if (!self.storage_.mutationMarkerUnchanged(self.marker_)) return false;
    if (!request.length) return true;
    auto file = self.storage_.open(self.path_, O_WRONLY);
    if (!file) return false;
    if (file.isDirectory() || file.fileSize64() != self.written_ || !file.seek64(self.written_)) {
      file.close();
      return false;
    }
    const size_t written = file.write(request.data, request.length);
    const bool synced = file.sync();
    const bool closed = file.close();
    if (written != request.length || !synced || !closed) return false;
    if (!self.storage_.captureMutationMarker(self.marker_)) return false;
    self.written_ += request.length;
    return true;
  }
  static bool finishUnderLock(void* context) {
    auto& self = *static_cast<ExclusiveStage*>(context);
    if (!self.storage_.mutationMarkerUnchanged(self.marker_)) return false;
    auto file = self.storage_.open(self.path_, O_RDONLY);
    if (!file) return false;
    const bool correct = !file.isDirectory() && file.fileSize64() == self.written_;
    const bool closed = file.close();
    return correct && closed && self.storage_.mutationMarkerUnchanged(self.marker_);
  }
  bool fail() {
    state_ = State::Failed;
    return false;
  }

  HalStorage& storage_;
  HalStorage::MutationMarker marker_;
  char path_[NAME_LENGTH + 2]{};
  uint64_t expected_ = 0;
  uint64_t written_ = 0;
  State state_ = State::Idle;
};
}  // namespace wirecheck
