#pragma once

#include <Print.h>
#include <fcntl.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

struct HostFileData {
  std::vector<uint8_t> bytes;
  std::string name;
  bool directory = false;
  bool failClose = false;
  bool failRead = false;
  bool failSync = false;
  size_t writeLimit = std::numeric_limits<size_t>::max();
  std::vector<std::shared_ptr<HostFileData>> entries;
};

class HalFile : public Print {
 public:
  HalFile() = default;
  explicit HalFile(std::shared_ptr<HostFileData> data) : data_(std::move(data)) {}

  int read(void* output, const size_t length) {
    if (!data_) return 0;
    if (data_->failRead) {
      data_->failRead = false;
      return -1;
    }
    const size_t readable = std::min(length, data_->bytes.size() - std::min(cursor_, data_->bytes.size()));
    if (readable > 0) std::copy_n(data_->bytes.data() + cursor_, readable, static_cast<uint8_t*>(output));
    cursor_ += readable;
    return static_cast<int>(readable);
  }

  size_t write(const uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* input, const size_t length) override {
    if (!data_) return 0;
    const size_t amount = std::min(length, data_->writeLimit);
    if (cursor_ + amount > data_->bytes.size()) data_->bytes.resize(cursor_ + amount);
    std::copy_n(input, amount, data_->bytes.begin() + static_cast<std::ptrdiff_t>(cursor_));
    cursor_ += amount;
    data_->writeLimit -= amount;
    return amount;
  }
  size_t write(const void* input, const size_t length) { return write(static_cast<const uint8_t*>(input), length); }

  void flush() {}
  bool sync() const { return data_ && !data_->failSync; }
  bool seekSet(size_t value) {
    cursor_ = value;
    return static_cast<bool>(data_);
  }
  bool close() {
    const bool ok = !data_ || !data_->failClose;
    if (data_) data_->failClose = false;
    data_.reset();
    cursor_ = 0;
    return ok;
  }
  size_t fileSize() const { return data_ ? data_->bytes.size() : 0; }
  bool isDirectory() const { return data_ && data_->directory; }
  size_t getName(char* output, size_t size) const {
    if (!data_ || size == 0) return 0;
    const size_t length = std::min(data_->name.size(), size - 1);
    std::copy_n(data_->name.data(), length, output);
    output[length] = 0;
    return length;
  }
  HalFile openNextFile() {
    if (!data_ || entryIndex_ >= data_->entries.size()) return {};
    return HalFile(data_->entries[entryIndex_++]);
  }
  explicit operator bool() const { return static_cast<bool>(data_); }

 private:
  std::shared_ptr<HostFileData> data_;
  size_t cursor_ = 0;
  size_t entryIndex_ = 0;
};

using FsFile = HalFile;

class HalStorage {
 public:
  enum class PathStatus { Present, Missing, Failed };
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }
  void reset() {
    files_.clear();
    directories_.clear();
    failRenameFrom_.clear();
    failAliasRenameFrom_.clear();
    failReadPaths_.clear();
    failRemovePath_.clear();
    failWriteLimits_.clear();
    failNewClosePaths_.clear();
    failNewSyncPaths_.clear();
    monitorHealthy_ = true;
    failOpen_ = false;
    failedOpenPath_.clear();
  }

  HalFile open(const char* path) {
    if (directories_.contains(path)) {
      auto data = std::make_shared<HostFileData>();
      data->directory = true;
      const std::string root(path);
      for (const auto& [name, entry] : files_) {
        if (name.substr(0, name.find_last_of('/')) == root) data->entries.push_back(entry);
      }
      for (const auto& name : directories_) {
        if (name.substr(0, name.find_last_of('/')) != root) continue;
        auto child = std::make_shared<HostFileData>();
        child->name = name.substr(name.find_last_of('/') + 1);
        child->directory = true;
        data->entries.push_back(child);
      }
      return HalFile(data);
    }
    HalFile file;
    openFileForRead("TEST", path, file);
    return file;
  }
  HalFile open(const char* path, int flags) {
    if (failOpen_ && (failOpenTarget_.empty() || failOpenTarget_ == path)) {
      failOpen_ = false;
      failedOpenPath_ = path;
      return {};
    }
    if (flags & O_CREAT) {
      if ((flags & O_EXCL) && exists(path)) return {};
      HalFile file;
      return openFileForWrite("TEST", path, file) ? std::move(file) : HalFile{};
    }
    HalFile file;
    return openFileForRead("TEST", path, file) ? std::move(file) : HalFile{};
  }
  bool withStorageLock(bool (*operation)(void*), void* context) { return operation && operation(context); }
  bool monitoredStorageHealthy() { return monitorHealthy_; }
  void setMonitorHealthy(bool healthy) { monitorHealthy_ = healthy; }
  void failNextOpen(const std::string& target = {}) {
    failOpen_ = true;
    failOpenTarget_ = target;
  }
  const std::string& failedOpenPath() const { return failedOpenPath_; }
  bool distinctFirstClusters(const char* first, const char* second) const {
    const auto a = files_.find(first), b = files_.find(second);
    return a != files_.end() && b != files_.end() && a->second != b->second;
  }
  PathStatus lookupPath(const char* path) const {
    if (failReadPaths_.contains(path)) return PathStatus::Failed;
    return exists(path) ? PathStatus::Present : PathStatus::Missing;
  }
  bool mkdir(const char* path, bool = true) {
    directories_.insert(path);
    return true;
  }
  bool exists(const char* path) const { return files_.contains(path) || directories_.contains(path); }
  bool remove(const char* path) {
    if (failRemovePath_ == path) {
      failRemovePath_.clear();
      return false;
    }
    return files_.erase(path) > 0;
  }

  bool rename(const char* from, const char* to) {
    if (failAliasRenameFrom_.erase(from) > 0) {
      const auto source = files_.find(from);
      if (source != files_.end() && !files_.contains(to)) files_[to] = source->second;
      return false;
    }
    if (failRenameFrom_.erase(from) > 0) {
      return false;
    }
    if (directories_.contains(from)) {
      if (exists(to)) return false;
      const std::string prefix = std::string(from) + "/";
      std::vector<std::pair<std::string, std::shared_ptr<HostFileData>>> moved;
      for (const auto& [path, data] : files_)
        if (path.starts_with(prefix)) moved.push_back({path, data});
      for (const auto& [path, data] : moved) {
        files_[std::string(to) + path.substr(std::string(from).size())] = data;
        files_.erase(path);
      }
      directories_.erase(from);
      directories_.insert(to);
      return true;
    }
    const auto source = files_.find(from);
    if (source == files_.end() || files_.contains(to)) return false;
    source->second->name = std::string(to).substr(std::string(to).find_last_of('/') + 1);
    files_[to] = source->second;
    files_.erase(source);
    return true;
  }

  bool removeDir(const char* path) {
    const std::string prefix = std::string(path) + "/";
    for (auto it = files_.begin(); it != files_.end();) {
      if (it->first.starts_with(prefix))
        it = files_.erase(it);
      else
        ++it;
    }
    for (auto it = directories_.begin(); it != directories_.end();) {
      if (*it == path || it->starts_with(prefix))
        it = directories_.erase(it);
      else
        ++it;
    }
    return true;
  }

  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    if (failReadPaths_.contains(path)) return false;
    const auto found = files_.find(path);
    if (found == files_.end()) return false;
    file = HalFile(found->second);
    return true;
  }

  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    auto data = std::make_shared<HostFileData>();
    data->name = path.substr(path.find_last_of('/') + 1);
    data->failClose = failNewClosePaths_.erase(path) > 0;
    data->failSync = failNewSyncPaths_.erase(path) > 0;
    const auto fail = failWriteLimits_.find(path);
    if (fail != failWriteLimits_.end()) {
      data->writeLimit = fail->second;
      failWriteLimits_.erase(fail);
    }
    files_[path] = data;
    directories_.insert(path.substr(0, path.find_last_of('/')));
    file = HalFile(std::move(data));
    return true;
  }

  bool aliasPath(const std::string& source, const std::string& target) {
    const auto found = files_.find(source);
    if (found == files_.end()) return false;
    files_[target] = found->second;
    return true;
  }

  void failNextNewCloseFor(const std::string& path) { failNewClosePaths_.insert(path); }
  void failNextNewSyncFor(const std::string& path) { failNewSyncPaths_.insert(path); }
  void failNextWriteFor(const std::string& path, size_t limit) { failWriteLimits_[path] = limit; }
  void failReadsFor(const std::string& path) { failReadPaths_.insert(path); }
  void failNextReadFor(const std::string& path) {
    const auto found = files_.find(path);
    if (found != files_.end()) found->second->failRead = true;
  }
  void failNextCloseFor(const std::string& path) {
    const auto found = files_.find(path);
    if (found != files_.end()) found->second->failClose = true;
  }
  void failNextRenameFrom(std::string path) { failRenameFrom_.insert(std::move(path)); }
  void failNextAliasRenameFrom(std::string path) { failAliasRenameFrom_.insert(std::move(path)); }
  void failNextRemove(std::string path) { failRemovePath_ = std::move(path); }

 private:
  bool monitorHealthy_ = true;
  bool failOpen_ = false;
  std::string failOpenTarget_;
  std::string failedOpenPath_;
  std::unordered_map<std::string, std::shared_ptr<HostFileData>> files_;
  std::unordered_set<std::string> directories_;
  std::unordered_set<std::string> failRenameFrom_;
  std::unordered_set<std::string> failAliasRenameFrom_;
  std::unordered_set<std::string> failReadPaths_;
  std::unordered_map<std::string, size_t> failWriteLimits_;
  std::unordered_set<std::string> failNewClosePaths_;
  std::unordered_set<std::string> failNewSyncPaths_;
  std::string failRemovePath_;
};

#ifndef Storage
#define Storage HalStorage::getInstance()
#endif
