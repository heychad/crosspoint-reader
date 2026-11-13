#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

constexpr int O_RDONLY = 0, O_CREAT = 1, O_EXCL = 2, O_WRONLY = 4;
namespace fake {
struct File {
  std::vector<uint8_t> bytes;
};
inline std::map<std::string, std::shared_ptr<File>> files;
inline std::set<std::string> directories{"/", "/.crosspoint", "/Authors"};
inline bool failWrite = false, failClose = false, failRead = false, failSync = false, missingParent = false,
            failRename = false, failMkdir = false, failRemove = false;
inline uint64_t epoch = 0;
inline unsigned writers = 0;
}  // namespace fake
class HalFile {
 public:
  std::shared_ptr<fake::File> file;
  bool error = false;
  bool directory = false;
  bool writable = false;
  size_t offset = 0;
  explicit operator bool() const { return bool(file); }
  size_t write(const uint8_t* data, size_t size) {
    ++fake::epoch;
    if (fake::failWrite) {
      size = std::min<size_t>(size, 2);
      error = true;
    }
    file->bytes.insert(file->bytes.end(), data, data + size);
    return size;
  }
  bool hasError() const { return error; }
  bool sync() {
    if (writable) ++fake::epoch;
    return !fake::failSync;
  }
  bool isDirectory() const { return directory; }
  uint64_t fileSize64() const { return file->bytes.size(); }
  bool seek64(uint64_t position) {
    if (position > file->bytes.size()) return false;
    offset = static_cast<size_t>(position);
    return true;
  }
  int read(void* output, size_t size) {
    if (fake::failRead) {
      error = true;
      return -1;
    }
    size = std::min(size, file->bytes.size() - offset);
    std::copy_n(file->bytes.data() + offset, size, static_cast<uint8_t*>(output));
    offset += size;
    return size;
  }
  bool close() {
    if (writable) {
      ++fake::epoch;
      --fake::writers;
      writable = false;
    }
    file.reset();
    return !fake::failClose;
  }
};
class HalStorage {
 public:
  enum class PathStatus { Missing, Present, Failed };
  struct MutationSnapshot {
    uint64_t epoch = 0;
  };
  struct MutationMarker {
    uint64_t epoch = 0;
  };
  bool beginMutationSnapshot(MutationSnapshot& s) {
    if (fake::writers) return false;
    s.epoch = fake::epoch;
    return true;
  }
  bool mutationSnapshotUnchanged(const MutationSnapshot& s) { return !fake::writers && s.epoch == fake::epoch; }
  bool captureMutationMarker(MutationMarker& m) {
    m.epoch = fake::epoch;
    return true;
  }
  bool mutationMarkerUnchanged(const MutationMarker& m) { return m.epoch == fake::epoch; }
  bool observedSdmmcHealthy() const { return true; }
  PathStatus lookupPath(const char* path) {
    if (std::string(path) == "/.crosspoint") return fake::missingParent ? PathStatus::Missing : PathStatus::Present;
    return fake::files.count(path) || fake::directories.count(path) ? PathStatus::Present : PathStatus::Missing;
  }
  bool withStorageLock(bool (*fn)(void*), void* context) { return fn(context); }
  bool mkdir(const char* path, bool recursive) {
    ++fake::epoch;
    if (recursive || fake::failMkdir || fake::directories.count(path) || fake::files.count(path)) return false;
    fake::directories.insert(path);
    return true;
  }
  bool rename(const char* source, const char* destination) {
    ++fake::epoch;
    if (fake::failRename || !fake::files.count(source) || fake::files.count(destination)) return false;
    fake::files[destination] = fake::files[source];
    fake::files.erase(source);
    return true;
  }
  bool remove(const char* path) {
    ++fake::epoch;
    if (fake::failRemove || !fake::files.count(path)) return false;
    fake::files.erase(path);
    return true;
  }
  HalFile openChecked(const char* path, int flags) {
    if (flags == 0) {
      HalFile file;
      if (fake::files.count(path))
        file.file = fake::files[path];
      else if (fake::directories.count(path)) {
        file.file = std::make_shared<fake::File>();
        file.directory = true;
      }
      return file;
    }
    if (flags != (O_CREAT | O_EXCL | O_WRONLY) || fake::files.count(path)) return {};
    ++fake::epoch;
    ++fake::writers;
    HalFile file;
    file.writable = true;
    file.file = std::make_shared<fake::File>();
    fake::files[path] = file.file;
    return file;
  }
};
