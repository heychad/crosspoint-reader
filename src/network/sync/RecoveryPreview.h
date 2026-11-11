#pragma once
#include "PublishIntentStore.h"
#include "StageDigest.h"

namespace syncpublish {
enum class PreviewResult { Observed, NoIntent, Damaged, Changed, Cancelled, Unavailable };
enum class StageState { Missing, Matches, Mismatch };
enum class DestinationState { Missing, Matches, Mismatch };
struct RecoveryPreview {
  static constexpr uint8_t NO_COPY_SLOT = 255;
  std::array<char, STAGE_BYTES + 1> stage{};
  std::array<char, MAX_DESTINATION + 1> destination{};
  uint64_t size = 0;
  StageState stageState = StageState::Missing;
  StageState copyState = StageState::Missing;
  uint8_t copySlot = NO_COPY_SLOT;
  DestinationState destinationState = DestinationState::Missing;
};
// Read-only point-in-time observation, never permission to publish, delete or
// recover. The caller owns output off a small task stack. Output changes only
// for Observed, and no snapshot is retained after the return.
PreviewResult previewRecovery(HalStorage& storage, RecoveryPreview& output, syncstage::DigestTick tick, void* context);
}  // namespace syncpublish
