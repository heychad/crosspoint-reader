#include "StageDigest.h"

#include <Memory.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "PublishIntent.h"
#include "StageName.h"
#include "StageWriter.h"

namespace syncstage {
namespace {
constexpr size_t BUFFER_BYTES = 4096;
struct HashContext {
  mbedtls_sha256_context value;
  HashContext() { mbedtls_sha256_init(&value); }
  ~HashContext() { mbedtls_sha256_free(&value); }
};
}  // namespace

static DigestResult hashPath(HalStorage& storage, const char* path, uint64_t expectedSize, uint8_t (&out)[32],
                             DigestTick tick, void* context) {
  HalStorage::MutationSnapshot snapshot;
  if (!storage.beginMutationSnapshot(snapshot)) return DigestResult::Unavailable;
  auto file = storage.openChecked(path, O_RDONLY);
  if (!file) return DigestResult::Unavailable;
  auto result = DigestResult::Ok;
  uint8_t computed[32];
  if (!storage.mutationSnapshotUnchanged(snapshot))
    result = DigestResult::Changed;
  else if (file.isDirectory() || file.hasError())
    result = DigestResult::IoError;
  else if (file.fileSize64() != expectedSize)
    result = DigestResult::WrongSize;
  else if (!tick(context))
    result = DigestResult::Cancelled;
  else {
    auto buffer = makeUniqueNoThrow<uint8_t[]>(BUFFER_BYTES);
    auto hash = makeUniqueNoThrow<HashContext>();
    if (!buffer || !hash)
      result = DigestResult::NoMemory;
    else if (mbedtls_sha256_starts(&hash->value, 0) != 0)
      result = DigestResult::HashError;
    else {
      uint64_t remaining = expectedSize;
      while (remaining && result == DigestResult::Ok) {
        const size_t count = static_cast<size_t>(std::min<uint64_t>(remaining, BUFFER_BYTES));
        if (!storage.mutationSnapshotUnchanged(snapshot))
          result = DigestResult::Changed;
        else if (file.read(buffer.get(), count) != static_cast<int>(count) || file.hasError())
          result = DigestResult::IoError;
        else if (!storage.mutationSnapshotUnchanged(snapshot))
          result = DigestResult::Changed;
        else if (mbedtls_sha256_update(&hash->value, buffer.get(), count) != 0)
          result = DigestResult::HashError;
        else {
          remaining -= count;
          if (!tick(context)) result = DigestResult::Cancelled;
        }
      }
      if (result == DigestResult::Ok && mbedtls_sha256_finish(&hash->value, computed) != 0)
        result = DigestResult::HashError;
    }
  }
  const bool clean = !file.hasError();
  const bool closed = file.close();
  if ((result == DigestResult::Ok || result == DigestResult::WrongSize) && (!clean || !closed))
    result = DigestResult::IoError;
  if ((result == DigestResult::Ok || result == DigestResult::WrongSize) && !storage.mutationSnapshotUnchanged(snapshot))
    result = DigestResult::Changed;
  if (result == DigestResult::Ok) memcpy(out, computed, sizeof(computed));
  return result;
}
static DigestResult hashNamedStage(HalStorage& storage, const char* name, uint64_t expectedSize, uint8_t (&out)[32],
                                   DigestTick tick, void* context, bool privateStage) {
  if (!name || !validName(name) || expectedSize > StageWriter::MAX_BYTES || !tick) return DigestResult::Invalid;
  char path[64];
  const int length = snprintf(path, sizeof(path), privateStage ? "/.crosspoint/%s" : "/%s", name);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(path)) return DigestResult::Invalid;
  return hashPath(storage, path, expectedSize, out, tick, context);
}
DigestResult hashStage(HalStorage& storage, const char* name, uint64_t expectedSize, uint8_t (&out)[32],
                       DigestTick tick, void* context) {
  return hashNamedStage(storage, name, expectedSize, out, tick, context, false);
}
DigestResult hashPrivateStage(HalStorage& storage, const char* name, uint64_t expectedSize, uint8_t (&out)[32],
                              DigestTick tick, void* context) {
  return hashPrivateStageSlot(storage, name, 0, expectedSize, out, tick, context);
}
bool privateStagePath(const char* name, unsigned slot, char (&path)[64]) {
  if (!name || !validName(name) || slot >= PRIVATE_COPY_SLOTS) return false;
  const int length = slot ? snprintf(path, sizeof(path), "/.crosspoint/%s-r%u", name, slot)
                          : snprintf(path, sizeof(path), "/.crosspoint/%s", name);
  return length >= 0 && static_cast<size_t>(length) < sizeof(path);
}
DigestResult hashPrivateStageSlot(HalStorage& storage, const char* name, unsigned slot, uint64_t expectedSize,
                                  uint8_t (&out)[32], DigestTick tick, void* context) {
  char path[64];
  if (!privateStagePath(name, slot, path) || expectedSize > StageWriter::MAX_BYTES || !tick)
    return DigestResult::Invalid;
  return hashPath(storage, path, expectedSize, out, tick, context);
}
DigestResult hashDestination(HalStorage& storage, const char* path, uint64_t expectedSize, uint8_t (&out)[32],
                             DigestTick tick, void* context) {
  if (!path || !syncpublish::validDestination(path) || expectedSize > StageWriter::MAX_BYTES || !tick)
    return DigestResult::Invalid;
  return hashPath(storage, path, expectedSize, out, tick, context);
}
}  // namespace syncstage
