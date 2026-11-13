#pragma once
#include <FsLib/FsFile.h>
#include <FsLib/FsVolume.h>

#include <cstdio>
#include <memory>
#include <string>

#include "SmallFileIdentity.h"

class HalFile {
 public:
  HalFile() = default;
  explicit HalFile(std::unique_ptr<FsBaseFile> file) : file_(std::move(file)) {}
  explicit operator bool() const { return file_ && file_->isOpen(); }
  size_t write(const uint8_t* bytes, size_t size) { return file_->write(bytes, size); }
  size_t write(const void* bytes, size_t size) { return file_->write(static_cast<const uint8_t*>(bytes), size); }
  size_t write(uint8_t byte) { return file_->write(&byte, 1); }
  void flush() { file_->sync(); }
  bool sync() { return file_->sync(); }
  bool close() { return file_->close(); }
  size_t fileSize() const { return file_->fileSize(); }
  size_t size() const { return fileSize(); }
  size_t position() const { return file_->curPosition(); }
  int available() const { return file_->available(); }
  uint64_t fileSize64() const { return file_->fileSize(); }
  bool isDirectory() const { return file_->isDir(); }
  bool seekSet(size_t offset) { return file_->seekSet(offset); }
  bool seek(size_t offset) { return seekSet(offset); }
  int read(void* out, size_t size) { return file_->read(out, size); }
  HalFile openNextFile() {
    auto next = std::make_unique<FsBaseFile>();
    if (!next->openNext(file_.get(), O_RDONLY)) return {};
    return HalFile(std::move(next));
  }
  size_t getName(char* name, size_t size) { return file_->getName(name, size); }
#if defined(SDFAT_REAL_MOVE_STORES)
  bool hasError() const { return !file_ || file_->getError() != 0; }
#endif

 private:
  std::unique_ptr<FsBaseFile> file_;
};
class HalStorage {
 public:
  enum class PathStatus { Missing, Present, Failed };
#if defined(SDFAT_REAL_MOVE_STORES)
  struct MutationSnapshot {};
  bool beginMutationSnapshot(MutationSnapshot&) { return true; }
  bool mutationSnapshotUnchanged(const MutationSnapshot&) { return true; }
#endif
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }
  void attach(FsVolume& volume) { volume_ = &volume; }
  PathStatus lookupPath(const char* path);
  bool exists(const char* path) { return lookupPath(path) == PathStatus::Present; }
  class StorageLock {};
  bool lookupNumberedChildren(const char* path, const char* prefix, uint64_t& present);
  FsFile openBatchRoot() {
    FsFile root;
    root.openRoot(volume_);
    return root;
  }
  FsFile openBatchDirectory(const char* path) {
    FsFile root, directory;
    if (!root.openRoot(volume_) || !directory.open(&root, path + 1, O_RDONLY) || !root.close()) return {};
    return directory;
  }
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
  HalFile openChecked(const char* path, oflag_t flags = O_RDONLY) { return open(path, flags); }
  bool openFileForRead(const char*, const char* path, HalFile& out) {
    out = open(path, O_RDONLY);
    return static_cast<bool>(out);
  }
  bool openFileForRead(const char* tag, const std::string& path, HalFile& out) {
    return openFileForRead(tag, path.c_str(), out);
  }
  bool withStorageLock(bool (*operation)(void*), void* context) { return operation && operation(context); }
  bool rename(const char* from, const char* to) { return volume_->rename(from, to); }
  bool mkdir(const char* path, bool parents = true) { return volume_->mkdir(path, parents); }
  bool remove(const char* path) { return volume_->remove(path); }
  bool removeDir(const char* path) { return volume_->rmdir(path); }
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
  bool settingsChainEligible(const char* path) {
    FsBaseFile file;
    if (!file.open(path, O_RDONLY)) return false;
    const bool eligible = settingsFileHasCheckedChain(file);
    return file.close() && eligible;
  }
  bool distinctSettingsChains(const char* first, const char* second) {
    FsBaseFile a, b;
    if (!a.open(first, O_RDONLY) || !b.open(second, O_RDONLY)) return false;
    const bool distinct = settingsFilesHaveDisjointChains(a, b);
    const bool ac = a.close(), bc = b.close();
    return distinct && ac && bc;
  }
  bool distinctMoveChains(const char* first, const char* second) {
    FsBaseFile a, b;
    if (!a.open(first, O_RDONLY) || !b.open(second, O_RDONLY)) return false;
    const bool distinct = moveFilesHaveIndependentStorage(a, b);
    const bool ac = a.close(), bc = b.close();
    return distinct && ac && bc;
  }

 private:
  FsVolume* volume_ = nullptr;
};
#define Storage HalStorage::getInstance()
