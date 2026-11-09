#include <array>
#include <cassert>
#include <cstring>
#include <vector>

#include "CooperativeDirectoryScan.h"
using namespace wirecheck;
using Record = std::array<uint8_t, 32>;
struct Fake {
  std::vector<Record> records;
  size_t at = 0, readCount = 0, emitted = 0, closes = 0;
  int cancelAt = -1, mutateAt = -1, errorAt = -1, shortAt = -1, failEmit = -1;
  bool changed = false, closeFails = false, changeOnClose = false, cancelOnClose = false, changeOnEmit = false;
  static ReadResult read(void* p, uint8_t* out) {
    auto& f = *static_cast<Fake*>(p);
    const int index = static_cast<int>(f.readCount++);
    if (index == f.mutateAt) {
      f.changed = true;
      return ReadResult::Changed;
    }
    if (index == f.errorAt) return ReadResult::Error;
    if (index == f.shortAt) return ReadResult::Error;
    if (f.at == f.records.size()) return ReadResult::End;
    std::memcpy(out, f.records[f.at++].data(), 32);
    return ReadResult::Record;
  }
  static bool valid(void* p) { return !static_cast<Fake*>(p)->changed; }
  static bool tick(void* p) {
    auto& f = *static_cast<Fake*>(p);
    return static_cast<int>(f.readCount) != f.cancelAt;
  }
  static bool emit(void* p, const DirectoryEntry& e) {
    auto& f = *static_cast<Fake*>(p);
    assert(e.name[0] == 'B');
    const auto index = f.emitted++;
    if (f.changeOnEmit) f.changed = true;
    return static_cast<int>(index) != f.failEmit;
  }
  static bool close(void* p) {
    auto& f = *static_cast<Fake*>(p);
    ++f.closes;
    if (f.changeOnClose) f.changed = true;
    if (f.cancelOnClose) f.cancelAt = static_cast<int>(f.readCount);
    return !f.closeFails;
  }
  ScanOps ops() { return {this, read, valid, tick, emit, close}; }
};
Fake sample() {
  Fake f;
  for (int i = 0; i < 3; ++i) {
    Record r{};
    std::memcpy(r.data(), "BOOK0000TXT", 11);
    r[7] = '0' + i;
    r[11] = 0x20;
    f.records.push_back(r);
  }
  f.records.push_back({});
  return f;
}
ScanResult run(CooperativeDirectoryScan& scan) {
  ScanResult status = ScanResult::Pending;
  while (status == ScanResult::Pending) status = scan.step();
  return status;
}
int main() {
  {
    auto f = sample();
    CooperativeDirectoryScan scan(Format::Fat, f.ops());
    assert(run(scan) == ScanResult::Complete);
    assert(f.emitted == 3 && f.closes == 1);
    assert(scan.step() == ScanResult::Complete);
    assert(f.closes == 1);
  }
  for (int point = 0; point < 4; ++point) {
    {
      auto f = sample();
      f.cancelAt = point;
      CooperativeDirectoryScan scan(Format::Fat, f.ops());
      assert(run(scan) == ScanResult::Failed);
      assert(f.closes == 1);
    }
    {
      auto f = sample();
      f.mutateAt = point;
      CooperativeDirectoryScan scan(Format::Fat, f.ops());
      assert(run(scan) == ScanResult::Failed);
      assert(f.closes == 1);
    }
    {
      auto f = sample();
      f.errorAt = point;
      CooperativeDirectoryScan scan(Format::Fat, f.ops());
      assert(run(scan) == ScanResult::Failed);
      assert(f.closes == 1);
    }
  }
  {
    auto f = sample();
    f.failEmit = 1;
    CooperativeDirectoryScan scan(Format::Fat, f.ops());
    assert(run(scan) == ScanResult::Failed);
    assert(f.closes == 1);
  }
  {
    auto f = sample();
    f.closeFails = true;
    CooperativeDirectoryScan scan(Format::Fat, f.ops());
    assert(run(scan) == ScanResult::Failed);
  }
  {
    auto f = sample();
    f.changeOnClose = true;
    CooperativeDirectoryScan scan(Format::Fat, f.ops());
    assert(run(scan) == ScanResult::Failed);
  }
  {
    auto f = sample();
    f.changeOnEmit = true;
    CooperativeDirectoryScan scan(Format::Fat, f.ops());
    assert(run(scan) == ScanResult::Failed);
  }
  {
    auto f = sample();
    f.cancelOnClose = true;
    CooperativeDirectoryScan scan(Format::Fat, f.ops());
    assert(run(scan) == ScanResult::Failed);
  }
  {
    auto f = sample();
    CooperativeDirectoryScan scan(Format::Fat, f.ops());
    scan.cancel();
    assert(scan.step() == ScanResult::Failed);
    assert(f.closes == 1);
  }
  {
    auto f = sample();
    {
      CooperativeDirectoryScan scan(Format::Fat, f.ops());
      assert(scan.step() == ScanResult::Pending);
    }
    assert(f.closes == 1);
  }
}
