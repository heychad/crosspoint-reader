#include "FileChecksum.h"

#include <CheckedStoragePath.h>
#include <HiddenItems.h>
#include <Memory.h>
#include <mbedtls/sha256.h>
#include <strings.h>

#include <algorithm>
#include <cstring>

#include "ProtectedPaths.h"

namespace filechecksum {
bool validPath(std::string_view path) {
  if (path.size() < 2 || path.size() > MAX_PATH_BYTES || path.front() != '/' || path.back() == '/') return false;
  size_t depth = 0;
  for (size_t start = 1; start < path.size();) {
    const size_t slash = path.find('/', start);
    const auto part = path.substr(start, (slash == path.npos ? path.size() : slash) - start);
    if (part.empty() || part.front() == '.' || ++depth > 32 || !validCheckedStorageComponent(part)) return false;
    if (protectedpaths::isShortAlias(part.data(), part.data() + part.size())) return false;
    for (const char* hidden : FsHelpers::HIDDEN_ITEMS)
      if (strlen(hidden) == part.size() && strncasecmp(part.data(), hidden, part.size()) == 0) return false;
    if (slash == path.npos) break;
    start = slash + 1;
  }
  return true;
}

bool parseSize(std::string_view text, uint64_t& size) {
  if (text.empty() || (text.size() > 1 && text.front() == '0')) return false;
  uint64_t parsed = 0;
  for (const char ch : text) {
    if (ch < '0' || ch > '9' || parsed > (MAX_FILE_BYTES - (ch - '0')) / 10) return false;
    parsed = parsed * 10 + ch - '0';
  }
  size = parsed;
  return true;
}

const char* stateName(State state) {
  switch (state) {
    case State::Pending:
      return "pending";
    case State::Complete:
      return "complete";
    case State::Cancelled:
      return "cancelled";
    case State::Changed:
      return "changed";
    case State::NotFound:
      return "not_found";
    case State::Invalid:
      return "invalid";
    case State::WrongSize:
      return "wrong_size";
    case State::IoError:
      return "io_error";
    case State::NoMemory:
      return "no_memory";
    case State::Unavailable:
      return "unavailable";
    case State::HashError:
      return "hash_error";
  }
  return "unavailable";
}

struct Job::Work {
  mbedtls_sha256_context hash;
  uint8_t buffer[4096];
  Work() { mbedtls_sha256_init(&hash); }
  ~Work() { mbedtls_sha256_free(&hash); }
};

Job::Job(HalStorage& storage, std::string path, uint64_t size)
    : storage_(storage), path_(std::move(path)), size_(size) {
  if (!validPath(path_) || size > MAX_FILE_BYTES) return;
  work_ = makeUniqueNoThrow<Work>();
  if (!work_) {
    state_ = State::NoMemory;
    return;
  }
  if (!storage_.withStorageLock(beginLocked, this)) state_ = State::Unavailable;
  if (state_ != State::Pending) work_.reset();
}
Job::~Job() = default;

State Job::inspectFile(HalFile& file) {
  if (!storage_.mutationSnapshotUnchanged(snapshot_)) return State::Changed;
  const auto presence = storage_.lookupPath(path_.c_str());
  if (presence == HalStorage::PathStatus::Missing) return State::NotFound;
  if (presence != HalStorage::PathStatus::Present) return State::Unavailable;
  file = storage_.openChecked(path_.c_str(), O_RDONLY);
  if (!file) return State::IoError;
  if (file.isDirectory() || file.hasError()) return State::IoError;
  return file.fileSize64() == size_ ? State::Pending : State::WrongSize;
}

bool Job::beginLocked(void* raw) {
  auto& self = *static_cast<Job*>(raw);
  if (!self.storage_.beginMutationSnapshot(self.snapshot_)) {
    self.state_ = State::Unavailable;
    return true;
  }
  HalFile file;
  self.state_ = self.inspectFile(file);
  if (file && !file.close()) self.state_ = State::IoError;
  if (!self.storage_.mutationSnapshotUnchanged(self.snapshot_)) self.state_ = State::Changed;
  if (self.state_ == State::Pending && mbedtls_sha256_starts(&self.work_->hash, 0) != 0) self.state_ = State::HashError;
  return true;
}

bool Job::advanceLocked(void* raw) {
  auto& self = *static_cast<Job*>(raw);
  HalFile file;
  self.state_ = self.inspectFile(file);
  if (self.state_ == State::Pending && !file.seek64(self.processed_)) self.state_ = State::IoError;
  size_t budget = SLICE_BYTES;
  while (self.state_ == State::Pending && self.processed_ < self.size_ && budget) {
    const size_t count = static_cast<size_t>(
        std::min<uint64_t>(std::min(budget, sizeof(self.work_->buffer)), self.size_ - self.processed_));
    if (file.read(self.work_->buffer, count) != static_cast<int>(count) || file.hasError()) {
      self.state_ = State::IoError;
      break;
    }
    if (mbedtls_sha256_update(&self.work_->hash, self.work_->buffer, count) != 0) {
      self.state_ = State::HashError;
      break;
    }
    self.processed_ += count;
    budget -= count;
  }
  if (file && file.hasError()) self.state_ = State::IoError;
  if (file && self.state_ == State::Pending && file.fileSize64() != self.size_) self.state_ = State::WrongSize;
  if (file && !file.close()) self.state_ = State::IoError;
  if (!self.storage_.mutationSnapshotUnchanged(self.snapshot_)) self.state_ = State::Changed;
  if (self.state_ == State::Pending && self.processed_ == self.size_) {
    uint8_t bytes[32];
    if (mbedtls_sha256_finish(&self.work_->hash, bytes) != 0) {
      self.state_ = State::HashError;
    } else {
      constexpr char HEX_DIGITS[] = "0123456789abcdef";
      for (size_t i = 0; i < sizeof(bytes); ++i) {
        self.digest_[i * 2] = HEX_DIGITS[bytes[i] >> 4];
        self.digest_[i * 2 + 1] = HEX_DIGITS[bytes[i] & 15];
      }
      self.state_ = State::Complete;
    }
  }
  return true;
}

void Job::advance() {
  if (state_ != State::Pending) return;
  if (!storage_.withStorageLock(advanceLocked, this)) state_ = State::Unavailable;
  if (state_ != State::Pending) work_.reset();
}
void Job::finish(State state) {
  state_ = state;
  digest_[0] = '\0';
  work_.reset();
}
void Job::cancel() {
  if (state_ == State::Pending) finish(State::Cancelled);
}
State Job::state() {
  if (state_ == State::Complete && !storage_.mutationSnapshotUnchanged(snapshot_)) finish(State::Changed);
  return state_;
}
const char* Job::digest() { return state() == State::Complete ? digest_ : nullptr; }
}  // namespace filechecksum
