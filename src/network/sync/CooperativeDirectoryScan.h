#pragma once
#include "DirectoryEnumerator.h"

namespace wirecheck {
// Every callback is supplied by the eventual storage/network integration.
// readRecord must validate the same snapshot while holding the storage lock
// around its single record read. Emits are provisional until completion.
enum class ReadResult { Record, End, Error, Changed };
enum class ScanResult { Pending, Complete, Failed };
struct ScanOps {
  void* context;
  ReadResult (*readRecord)(void*, uint8_t* record);
  bool (*snapshotValid)(void*);
  bool (*keepGoing)(void*);
  bool (*emit)(void*, const DirectoryEntry&);
  bool (*close)(void*);
};
class CooperativeDirectoryScan {
 public:
  CooperativeDirectoryScan(Format format, ScanOps ops) : enumerator_(format), ops_(ops) {}
  ~CooperativeDirectoryScan() { cancel(); }
  CooperativeDirectoryScan(const CooperativeDirectoryScan&) = delete;
  CooperativeDirectoryScan& operator=(const CooperativeDirectoryScan&) = delete;
  ScanResult step() {
    if (result_ != ScanResult::Pending) return result_;
    if (!ops_.readRecord || !ops_.snapshotValid || !ops_.keepGoing || !ops_.emit || !ops_.close ||
        !ops_.keepGoing(ops_.context) || !ops_.snapshotValid(ops_.context))
      return terminate(false);
    uint8_t bytes[32];
    const auto status = ops_.readRecord(ops_.context, bytes);
    if (status == ReadResult::Error || status == ReadResult::Changed) return terminate(false);
    const auto decoded =
        status == ReadResult::End ? enumerator_.finish() : enumerator_.accept(bytes, sizeof(bytes), entry_);
    if (decoded == Step::Error) return terminate(false);
    if (decoded == Step::Entry && !ops_.emit(ops_.context, entry_)) return terminate(false);
    if (decoded == Step::End) return terminate(ops_.snapshotValid(ops_.context));
    return ScanResult::Pending;
  }
  // The callback context must outlive this object, including its destructor.
  void cancel() {
    if (result_ == ScanResult::Pending) terminate(false);
  }

 private:
  ScanResult terminate(bool complete) {
    if (complete && (!ops_.keepGoing || !ops_.keepGoing(ops_.context))) complete = false;
    if (ops_.close && !ops_.close(ops_.context)) complete = false;
    if (complete && (!ops_.snapshotValid || !ops_.snapshotValid(ops_.context))) complete = false;
    if (complete && (!ops_.keepGoing || !ops_.keepGoing(ops_.context))) complete = false;
    return result_ = complete ? ScanResult::Complete : ScanResult::Failed;
  }
  DirectoryEnumerator enumerator_;
  DirectoryEntry entry_;
  ScanOps ops_;
  ScanResult result_ = ScanResult::Pending;
};
}  // namespace wirecheck
