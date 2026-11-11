#include "StageWriter.h"

#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "StageName.h"

bool StageWriter::begin(const char* name, const uint64_t expectedSize) {
  if (state_ != State::Idle) {
    abort();
    return false;
  }
  if (!storage_.observedSdmmcHealthy() || !name || !syncstage::validName(name) || expectedSize > MAX_BYTES) {
    state_ = State::Failed;
    return false;
  }
  buffer_ = makeUniqueNoThrow<uint8_t[]>(BUFFER_BYTES);
  if (!buffer_) {
    state_ = State::Failed;
    return false;
  }
  char path[48];
  const int length = snprintf(path, sizeof(path), "/%s", name);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(path)) {
    abort();
    return false;
  }
  struct CreateContext {
    HalStorage& storage;
    const char* path;
    HalFile& file;
  } context{storage_, path, file_};
  const auto create = [](void* opaque) {
    auto& ctx = *static_cast<CreateContext*>(opaque);
    if (!ctx.storage.observedSdmmcHealthy() || ctx.storage.lookupPath(ctx.path) != HalStorage::PathStatus::Missing ||
        !ctx.storage.observedSdmmcHealthy())
      return false;
    ctx.file = ctx.storage.openChecked(ctx.path, O_CREAT | O_EXCL | O_WRONLY);
    return static_cast<bool>(ctx.file) && ctx.storage.observedSdmmcHealthy();
  };
  if (!storage_.withStorageLock(create, &context)) {
    abort();
    return false;
  }
  expected_ = expectedSize;
  received_ = buffered_ = 0;
  state_ = State::Receiving;
  return true;
}

bool StageWriter::flush() {
  if (!buffered_) return true;
  const bool ok = file_ && file_.write(buffer_.get(), buffered_) == buffered_ && !file_.hasError() &&
                  storage_.observedSdmmcHealthy();
  buffered_ = 0;
  return ok;
}

bool StageWriter::write(const uint8_t* data, size_t size) {
  if (state_ != State::Receiving || !file_ || !buffer_ || (!data && size) || size > expected_ - received_) {
    abort();
    return false;
  }
  received_ += size;
  while (size) {
    const size_t count = std::min(size, BUFFER_BYTES - buffered_);
    memcpy(buffer_.get() + buffered_, data, count);
    buffered_ += count;
    data += count;
    size -= count;
    if (buffered_ == BUFFER_BYTES && !flush()) {
      abort();
      return false;
    }
  }
  return true;
}

bool StageWriter::finish() {
  if (state_ != State::Receiving || !file_ || !buffer_ || received_ != expected_) {
    abort();
    return false;
  }
  const bool flushed = flush();
  const bool clean = !file_.hasError();
  const bool closed = file_.close();
  buffer_.reset();
  state_ = flushed && clean && closed && storage_.observedSdmmcHealthy() ? State::Complete : State::Failed;
  return state_ == State::Complete;
}

void StageWriter::abort() {
  buffered_ = 0;
  if (file_) file_.close();
  buffer_.reset();
  state_ = State::Failed;
}

bool StageWriter::takeResult() {
  const bool success = state_ == State::Complete && storage_.observedSdmmcHealthy();
  if (state_ == State::Receiving) abort();
  state_ = State::Idle;
  return success;
}
