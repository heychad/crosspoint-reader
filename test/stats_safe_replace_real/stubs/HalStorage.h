#pragma once
#include <FsLib/FsFile.h>
#include <FsLib/FsVolume.h>

#include <memory>
#include <string>

#include "SmallFileIdentity.h"

class HalFile {
 public:
  HalFile() = default;
  explicit HalFile(std::unique_ptr<FsBaseFile> file) : file_(std::move(file)) {}
  explicit operator bool() const { return file_ && file_->isOpen(); }
  size_t write(const uint8_t* bytes, size_t size) { return file_->write(bytes, size); }
  bool sync() { return file_->sync(); }
  bool close() { return file_->close(); }
  size_t fileSize() const { return file_->fileSize(); }
  int read(void* out, size_t size) { return file_->read(out, size); }

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
  HalFile open(const char* path, oflag_t flags) {
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
  bool withStorageLock(bool (*operation)(void*), void* context) { return operation && operation(context); }
  bool rename(const char* from, const char* to) { return volume_->rename(from, to); }
  bool remove(const char* path) { return volume_->remove(path); }
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
  bool distinctSettingsClusters(const char* first, const char* second) {
    FsBaseFile a, b;
    if (!a.open(first, O_RDONLY) || !b.open(second, O_RDONLY)) return false;
    const bool distinct = settingsFilesHaveDifferentFirstClusters(a, b);
    const bool ac = a.close(), bc = b.close();
    return distinct && ac && bc;
  }
  bool settingsSingleClusterEligible(const char* path) {
    FsBaseFile file;
    if (!file.open(path, O_RDONLY)) return false;
    const bool eligible = file.settingsSingleClusterChain();
    return file.close() && eligible;
  }

 private:
  FsVolume* volume_ = nullptr;
};
#define Storage HalStorage::getInstance()
