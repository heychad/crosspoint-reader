#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <new>
#include <type_traits>

#include "StorageMutationFence.h"

using Fence = checkedscan::StorageMutationFence;

TEST(StorageMutationFence, UnchangedReadOnlySnapshotRemainsValid) {
  Fence fence;
  Fence::Snapshot snapshot;
  EXPECT_FALSE(fence.unchanged(snapshot));
  ASSERT_TRUE(fence.beginSnapshot(snapshot));
  EXPECT_TRUE(fence.unchanged(snapshot));
  Fence other;
  EXPECT_FALSE(other.unchanged(snapshot));
  Fence::Snapshot second;
  ASSERT_TRUE(fence.beginSnapshot(second));
  EXPECT_TRUE(fence.unchanged(snapshot));
  EXPECT_TRUE(fence.unchanged(second));
}

TEST(StorageMutationFence, AttemptedMutationInvalidatesSnapshot) {
  Fence fence;
  Fence::Snapshot snapshot;
  ASSERT_TRUE(fence.beginSnapshot(snapshot));
  fence.mutationAttempt();  // Even a subsequently failed operation may have changed storage.
  EXPECT_FALSE(fence.unchanged(snapshot));
  Fence::Snapshot next;
  ASSERT_TRUE(fence.beginSnapshot(next));
  EXPECT_TRUE(fence.unchanged(next));
}

TEST(StorageMutationFence, ExistingAndNewWritersPreventAStableSnapshot) {
  Fence fence;
  Fence::Snapshot before;
  ASSERT_TRUE(fence.beginSnapshot(before));
  Fence::WriterLease first, second;
  ASSERT_TRUE(fence.writerOpened(first));
  EXPECT_FALSE(fence.unchanged(before));
  Fence::Snapshot whileOpen;
  EXPECT_FALSE(fence.beginSnapshot(whileOpen));
  EXPECT_FALSE(fence.unchanged(whileOpen));
  ASSERT_TRUE(fence.writerOpened(second));
  ASSERT_TRUE(fence.writerClosed(first));
  EXPECT_FALSE(fence.beginSnapshot(whileOpen));
  ASSERT_TRUE(fence.writerClosed(second));
  ASSERT_TRUE(fence.beginSnapshot(whileOpen));
  EXPECT_TRUE(fence.unchanged(whileOpen));
  EXPECT_FALSE(fence.unchanged(before));
}

TEST(StorageMutationFence, MarkerDetectsMutationsWhileWriterIsOpen) {
  Fence fence;
  Fence::WriterLease writer;
  ASSERT_TRUE(fence.writerOpened(writer));
  Fence::Snapshot snapshot;
  EXPECT_FALSE(fence.beginSnapshot(snapshot));
  Fence::Marker marker;
  ASSERT_TRUE(fence.captureMarker(marker));
  EXPECT_TRUE(fence.markerUnchanged(marker));
  Fence other;
  EXPECT_FALSE(other.markerUnchanged(marker));
  fence.mutationAttempt();
  EXPECT_FALSE(fence.markerUnchanged(marker));
  ASSERT_TRUE(fence.captureMarker(marker));
  ASSERT_TRUE(fence.writerClosed(writer));
  EXPECT_FALSE(fence.markerUnchanged(marker));
  ASSERT_TRUE(fence.captureMarker(marker));
  fence.poison();
  EXPECT_FALSE(fence.markerUnchanged(marker));
  EXPECT_FALSE(fence.captureMarker(marker));
}

TEST(StorageMutationFence, WriterMisuseAndEpochOverflowFailClosed) {
  Fence broken;
  Fence::Snapshot before;
  ASSERT_TRUE(broken.beginSnapshot(before));
  Fence::WriterLease absent;
  EXPECT_FALSE(broken.writerClosed(absent));
  EXPECT_FALSE(broken.unchanged(before));
  EXPECT_FALSE(broken.beginSnapshot(before));
  Fence nearLimit(std::numeric_limits<uint64_t>::max() - 1);
  Fence::Snapshot last;
  ASSERT_TRUE(nearLimit.beginSnapshot(last));
  nearLimit.mutationAttempt();
  EXPECT_FALSE(nearLimit.unchanged(last));
  ASSERT_TRUE(nearLimit.beginSnapshot(last));
  nearLimit.mutationAttempt();
  EXPECT_FALSE(nearLimit.unchanged(last));
  EXPECT_FALSE(nearLimit.beginSnapshot(last));
}

TEST(StorageMutationFence, DuplicateOrForeignCloseCannotHideAnOpenWriter) {
  static_assert(!std::is_copy_constructible_v<Fence::WriterLease>);
  static_assert(!std::is_move_constructible_v<Fence::WriterLease>);
  Fence fence;
  Fence::WriterLease first, second;
  ASSERT_TRUE(fence.writerOpened(first));
  ASSERT_TRUE(fence.writerOpened(second));
  ASSERT_TRUE(fence.writerClosed(first));
  EXPECT_FALSE(fence.writerClosed(first));
  Fence::Snapshot snapshot;
  EXPECT_FALSE(fence.beginSnapshot(snapshot));
  Fence other;
  EXPECT_FALSE(other.writerClosed(second));
  EXPECT_FALSE(other.beginSnapshot(snapshot));
}

TEST(StorageMutationFence, ReconstructingAtSameAddressInvalidatesOldTokens) {
  alignas(Fence) unsigned char memory[sizeof(Fence)];
  auto* first = new (memory) Fence();
  Fence::Snapshot old;
  ASSERT_TRUE(first->beginSnapshot(old));
  ASSERT_TRUE(first->unchanged(old));
  first->~Fence();
  auto* second = new (memory) Fence();
  EXPECT_FALSE(second->unchanged(old));
  Fence::Snapshot current;
  ASSERT_TRUE(second->beginSnapshot(current));
  EXPECT_TRUE(second->unchanged(current));
  second->~Fence();
}

TEST(StorageMutationFence, UncertainStorageStatePermanentlyInvalidatesSnapshots) {
  Fence fence;
  Fence::Snapshot before;
  ASSERT_TRUE(fence.beginSnapshot(before));
  fence.poison();
  EXPECT_FALSE(fence.unchanged(before));
  EXPECT_FALSE(fence.beginSnapshot(before));
}
