#pragma once
#include <HalStorage.h>

#include <cstring>
#include <string>
#include <vector>

namespace wirecheck {
enum class ScanResult { Pending, Complete, Failed };
struct DirectoryEntry {
  char name[766]{};
  uint64_t size = 0;
  bool directory = false;
};
class PathDirectoryScan {
 public:
  struct Callbacks {
    void* context;
    bool (*keepGoing)(void*);
    bool (*emit)(void*, const DirectoryEntry&);
  };
  PathDirectoryScan(HalStorage& storage, const HalStorage::MutationSnapshot& snapshot, const char* path,
                    Callbacks callbacks)
      : storage_(storage), snapshot_(snapshot), callbacks_(callbacks) {
    std::string prefix(path);
    if (prefix != "/") prefix += '/';
    const auto add = [&](const std::string& item, bool directory) {
      if (!item.starts_with(prefix)) return;
      auto rest = item.substr(prefix.size());
      if (rest.empty() || rest.find('/') != std::string::npos) return;
      DirectoryEntry e;
      std::strncpy(e.name, rest.c_str(), sizeof(e.name) - 1);
      e.directory = directory;
      entries_.push_back(e);
    };
    for (const auto& item : fake::directories) add(item, true);
    for (const auto& item : fake::files) add(item.first, false);
  }
  ScanResult step() {
    if (!callbacks_.keepGoing(callbacks_.context) || !storage_.mutationSnapshotUnchanged(snapshot_))
      return ScanResult::Failed;
    if (pos_ == entries_.size()) return ScanResult::Complete;
    return callbacks_.emit(callbacks_.context, entries_[pos_++]) ? ScanResult::Pending : ScanResult::Failed;
  }

 private:
  HalStorage& storage_;
  HalStorage::MutationSnapshot snapshot_;
  Callbacks callbacks_;
  std::vector<DirectoryEntry> entries_;
  size_t pos_ = 0;
};
}  // namespace wirecheck
