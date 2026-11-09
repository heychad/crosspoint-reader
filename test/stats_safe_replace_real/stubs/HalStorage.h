#pragma once
#include <FsLib/FsFile.h>
#include <FsLib/FsVolume.h>

#include <cstdio>
#include <memory>
#include <string>

#include "SmallFileIdentity.h"

inline bool traceDateIo = false;

class HalFile {
 public:
  HalFile() = default;
  explicit HalFile(std::unique_ptr<FsBaseFile> file) : file_(std::move(file)) {}
  explicit operator bool() const { return file_ && file_->isOpen(); }
  size_t write(const uint8_t* bytes, size_t size) { return file_->write(bytes, size); }
  size_t write(uint8_t byte) { return write(&byte, 1); }
  void flush() { (void)file_->sync(); }
  bool sync() { return file_->sync(); }
  bool close() { return file_->close(); }
  size_t fileSize() const { return file_->fileSize(); }
  int read(void* out, size_t size) { return file_->read(out, size); }
  bool isDirectory() const { return file_ && file_->isDir(); }
  size_t getName(char* out, size_t size) { return file_ ? file_->getName(out, size) : 0; }
  HalFile openNextFile() {
    auto next = std::make_unique<FsBaseFile>();
    return file_ && next->openNext(file_.get()) ? HalFile(std::move(next)) : HalFile{};
  }

 private:
  std::unique_ptr<FsBaseFile> file_;
};
class HalStorage {
 public:
  enum class PathStatus { Missing, Present, Failed };
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }
  void attach(FsVolume& volume) { volume_ = &volume; }
  void noteIoFailure() { healthy_ = false; }
  void resetMonitor() { healthy_ = true; }
  bool monitoredStorageHealthy() const { return healthy_; }
  PathStatus lookupPath(const char* path) {
    if (!volume_ || !path || path[0] != '/' || !path[1]) return PathStatus::Failed;
    FsBaseFile root, file;
    if (!root.openRoot(volume_) || !root.enableStrictLookup()) return PathStatus::Failed;
    const bool found = file.open(&root, path + 1, O_RDONLY);
    const bool failed = root.getError() != 0;
    const bool childClosed = !found || file.close();
    const bool rootClosed = root.close();
    return failed || !rootClosed || !childClosed ? PathStatus::Failed
           : found                               ? PathStatus::Present
                                                 : PathStatus::Missing;
  }
  bool exists(const char* path) { return lookupPath(path) == PathStatus::Present; }
  HalFile open(const char* path, oflag_t flags = O_RDONLY) {
    if (!volume_ || !path || path[0] != '/' || !path[1]) return {};
    FsBaseFile root;
    if (!root.openRoot(volume_)) return {};
    auto f = std::make_unique<FsBaseFile>();
    const bool opened = f->open(&root, path + 1, flags);
    const bool closed = root.close();
    if (!opened || !closed) return {};
    return HalFile(std::move(f));
  }
  bool openFileForRead(const char*, const char* path, HalFile& out) {
    out = open(path, O_RDONLY);
    return static_cast<bool>(out);
  }
  bool openFileForRead(const char* module, const std::string& path, HalFile& out) {
    return openFileForRead(module, path.c_str(), out);
  }
  bool mkdir(const char* path, bool recursive = true) { return volume_ && volume_->mkdir(path, recursive); }
  bool removeDir(const char* path) { return volume_ && volume_->rmdir(path); }
  bool withStorageLock(bool (*operation)(void*), void* context) { return operation && operation(context); }
  bool rename(const char* from, const char* to) {
    const bool ok = volume_->rename(from, to);
    if (traceDateIo) std::fprintf(stderr, "TRACE rename %s -> %s ok=%d healthy=%d\n", from, to, ok, healthy_);
    return ok;
  }
  bool remove(const char* path) {
    const bool ok = volume_->remove(path);
    if (traceDateIo) std::fprintf(stderr, "TRACE remove %s ok=%d healthy=%d\n", path, ok, healthy_);
    return ok;
  }
  bool distinctFirstClusters(const char* first, const char* second) {
    auto left = open(first, O_RDONLY), right = open(second, O_RDONLY);
    if (!left || !right) return false;
    // Match the production policy using FsBaseFile through a separate checked open.
    FsBaseFile a, b;
    if (!a.open(first, O_RDONLY) || !b.open(second, O_RDONLY)) return false;
    const bool distinct = smallFilesHaveDifferentFirstClusters(a, b);
    const bool ac = a.close(), bc = b.close();
    const bool lc = left.close(), rc = right.close();
    return distinct && ac && bc && lc && rc;
  }

 private:
  FsVolume* volume_ = nullptr;
  bool healthy_ = true;
};
#define Storage HalStorage::getInstance()
