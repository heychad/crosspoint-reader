#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace filechecksum {
enum class State {
  Pending,
  Complete,
  Cancelled,
  Changed,
  NotFound,
  Invalid,
  WrongSize,
  IoError,
  NoMemory,
  Unavailable,
  HashError
};
constexpr size_t MAX_PATH_BYTES = 1024;
constexpr uint64_t MAX_FILE_BYTES = 0xffffffffULL;
constexpr size_t SLICE_BYTES = 16 * 1024;
bool validPath(std::string_view path);
bool parseSize(std::string_view text, uint64_t& size);
const char* stateName(State state);

// One read-only job. Advance on the File Transfer main task, with no retained
// file handle between slices. Any observed HAL mutation invalidates its result.
class Job {
 public:
  Job(HalStorage& storage, std::string path, uint64_t size);
  ~Job();
  Job(const Job&) = delete;
  Job& operator=(const Job&) = delete;
  void advance();
  void cancel();
  State state();
  const std::string& path() const { return path_; }
  uint64_t size() const { return size_; }
  uint64_t processed() const { return processed_; }
  const char* digest();

 private:
  struct Work;
  static bool beginLocked(void* raw);
  static bool advanceLocked(void* raw);
  State inspectFile(HalFile& file);
  void finish(State state);
  HalStorage& storage_;
  std::string path_;
  uint64_t size_;
  uint64_t processed_ = 0;
  HalStorage::MutationSnapshot snapshot_{};
  State state_ = State::Invalid;
  char digest_[65]{};
  std::unique_ptr<Work> work_;
};
}  // namespace filechecksum
