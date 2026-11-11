#pragma once

#include <HalStorage.h>

#include <cstdint>

namespace syncstage {
enum class DigestResult { Ok, Invalid, Unavailable, Changed, IoError, WrongSize, NoMemory, Cancelled, HashError };
using DigestTick = bool (*)(void*);
constexpr unsigned PRIVATE_COPY_SLOTS = 4;
bool privateStagePath(const char* name, unsigned slot, char (&path)[64]);
// A successful digest describes one tracked storage snapshot only. It does not
// reserve the path or prove card identity; subsequent publication must recheck.
DigestResult hashStage(HalStorage& storage, const char* name, uint64_t expectedSize, uint8_t (&out)[32],
                       DigestTick tick, void* context);
DigestResult hashPrivateStage(HalStorage& storage, const char* name, uint64_t expectedSize, uint8_t (&out)[32],
                              DigestTick tick, void* context);
DigestResult hashPrivateStageSlot(HalStorage& storage, const char* name, unsigned slot, uint64_t expectedSize,
                                  uint8_t (&out)[32], DigestTick tick, void* context);
DigestResult hashDestination(HalStorage& storage, const char* path, uint64_t expectedSize, uint8_t (&out)[32],
                             DigestTick tick, void* context);
}  // namespace syncstage
