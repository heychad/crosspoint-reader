#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>

#include "CooperativeDirectoryScan.h"
#include "StorageMutationFence.h"

namespace {
using Fence = checkedscan::StorageMutationFence;
struct Fixture {
  Fence fence;
  Fence::Snapshot snapshot;
  std::array<std::array<uint8_t, 32>, 2> records{};
  size_t position = 0;
  int emitted = 0;
  int closed = 0;
  bool mutateOnEmit = false;

  Fixture() {
    std::memcpy(records[0].data(), "BOOK    EPUB", 11);
    records[0][11] = 0x20;
  }
  static wirecheck::ReadResult read(void* context, uint8_t* out) {
    auto& self = *static_cast<Fixture*>(context);
    if (!self.fence.unchanged(self.snapshot)) return wirecheck::ReadResult::Changed;
    if (self.position == self.records.size()) return wirecheck::ReadResult::End;
    std::copy(self.records[self.position].begin(), self.records[self.position].end(), out);
    ++self.position;
    return wirecheck::ReadResult::Record;
  }
  static bool valid(void* context) {
    auto& self = *static_cast<Fixture*>(context);
    return self.fence.unchanged(self.snapshot);
  }
  static bool active(void*) { return true; }
  static bool emit(void* context, const wirecheck::DirectoryEntry&) {
    auto& self = *static_cast<Fixture*>(context);
    ++self.emitted;
    if (self.mutateOnEmit) {
      Fence::WriterLease writer;
      if (!self.fence.writerOpened(writer) || !self.fence.writerClosed(writer)) return false;
    }
    return true;
  }
  static bool close(void* context) {
    ++static_cast<Fixture*>(context)->closed;
    return true;
  }
  wirecheck::ScanOps ops() { return {this, read, valid, active, emit, close}; }
};
wirecheck::ScanResult run(wirecheck::CooperativeDirectoryScan& scan) {
  auto result = wirecheck::ScanResult::Pending;
  while (result == wirecheck::ScanResult::Pending) result = scan.step();
  return result;
}
}  // namespace

TEST(InventorySnapshot, ReadOnlyScanCompletesWithUnchangedSnapshot) {
  Fixture fixture;
  ASSERT_TRUE(fixture.fence.beginSnapshot(fixture.snapshot));
  wirecheck::CooperativeDirectoryScan scan(wirecheck::Format::Fat, fixture.ops());
  EXPECT_EQ(run(scan), wirecheck::ScanResult::Complete);
  EXPECT_EQ(fixture.emitted, 1);
  EXPECT_EQ(fixture.closed, 1);
  EXPECT_TRUE(fixture.fence.unchanged(fixture.snapshot));
}

TEST(InventorySnapshot, CompletedWriterDuringScanInvalidatesProvisionalEntries) {
  Fixture fixture;
  ASSERT_TRUE(fixture.fence.beginSnapshot(fixture.snapshot));
  fixture.mutateOnEmit = true;
  wirecheck::CooperativeDirectoryScan scan(wirecheck::Format::Fat, fixture.ops());
  EXPECT_EQ(run(scan), wirecheck::ScanResult::Failed);
  EXPECT_EQ(fixture.emitted, 1);
  EXPECT_EQ(fixture.closed, 1);
  EXPECT_FALSE(fixture.fence.unchanged(fixture.snapshot));
}
