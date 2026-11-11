#include "DestinationGuard.h"

#include <CredentialIntegrity.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>
#include <functional>

#include "PublishIntent.h"

namespace syncpublish {
namespace {
constexpr size_t GUARD_BUFFER_BYTES = MAX_DESTINATION + 128;
struct Check {
  HalStorage& storage;
  HalStorage::MutationSnapshot snapshot;
  syncstage::DigestTick tick;
  void* context;
  char* path;
  DestinationGuardResult result = DestinationGuardResult::Clear;
  bool inspect() {
    if (!tick(context)) {
      result = DestinationGuardResult::Cancelled;
      return false;
    }
    const auto found = storage.lookupPath(path);
    if (!storage.mutationSnapshotUnchanged(snapshot)) {
      result = DestinationGuardResult::Changed;
      return false;
    }
    if (found == HalStorage::PathStatus::Present) {
      result = DestinationGuardResult::Conflict;
      return false;
    }
    if (found == HalStorage::PathStatus::Failed) {
      result = DestinationGuardResult::Unavailable;
      return false;
    }
    return true;
  }
  bool set(std::string_view text, const char* suffix = "") {
    const size_t n = strlen(suffix);
    if (text.size() + n >= GUARD_BUFFER_BYTES) {
      result = DestinationGuardResult::Unavailable;
      return false;
    }
    memcpy(path, text.data(), text.size());
    memcpy(path + text.size(), suffix, n + 1);
    return true;
  }
  bool numbered(const char* prefix, uint64_t key, const char* suffix = "") {
    const int n = snprintf(path, GUARD_BUFFER_BYTES, "%s%llu%s", prefix, static_cast<unsigned long long>(key), suffix);
    if (n < 0 || static_cast<size_t>(n) >= GUARD_BUFFER_BYTES) {
      result = DestinationGuardResult::Unavailable;
      return false;
    }
    return true;
  }
};
uint64_t fnv(std::string_view text) {
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned char ch : text) hash = (hash ^ ch) * 1099511628211ULL;
  return hash;
}
}  // namespace
DestinationGuardResult inspectDestinationState(HalStorage& storage, std::string_view destination,
                                               syncstage::DigestTick tick, void* context) {
  if (!validDestination(destination) || !tick) return DestinationGuardResult::Invalid;
  auto buffer = makeUniqueNoThrow<char[]>(GUARD_BUFFER_BYTES);
  if (!buffer) return DestinationGuardResult::Unavailable;
  Check check{storage, {}, tick, context, buffer.get()};
  if (!storage.beginMutationSnapshot(check.snapshot)) return DestinationGuardResult::Unavailable;
  for (const char* suffix : {"", ".key", ".rights", ".meta.json", ".part", ".tmp", ".new", ".bak"})
    if (!check.set(destination, suffix) || !check.inspect()) return check.result;
  const uint64_t native = static_cast<uint64_t>(std::hash<std::string_view>{}(destination));
  const uint64_t legacy = fnv(destination);
  for (const char* prefix : {"/.crosspoint/epub_", "/.crosspoint/reading_stats/epub_"})
    for (const uint64_t key : {native, legacy})
      if (!check.numbered(prefix, key) || !check.inspect()) return check.result;
  for (const char* suffix : {".bin", ".bin.bak", ".bin.tmp", ".bin.deleted", ".bin.migrate.bak"})
    if (!check.numbered("/.crosspoint/clippings/epub_", native, suffix) || !check.inspect()) return check.result;
  for (const uint64_t key : {static_cast<uint64_t>(credential_integrity::crc32(destination)), native})
    for (const char* suffix :
         {".bin", ".bin.move-tmp", ".bin.bak", ".bin.bak.move-tmp", ".bin.tmp", ".bin.tmp.move-tmp"})
      if (!check.numbered("/.crosspoint/bookmarks/epub_", key, suffix) || !check.inspect()) return check.result;
  const auto dot = destination.find_last_of('.');
  constexpr std::string_view prefix = "/.crosspoint/bookmarks/";
  const auto body = destination.substr(1, dot - 1);
  if (prefix.size() + body.size() + 5 >= GUARD_BUFFER_BYTES) return DestinationGuardResult::Unavailable;
  memcpy(buffer.get(), prefix.data(), prefix.size());
  for (size_t i = 0; i < body.size(); ++i) buffer[prefix.size() + i] = body[i] == '/' ? '_' : body[i];
  const size_t base = prefix.size() + body.size();
  for (const char* suffix :
       {".json", ".json.move-tmp", ".json.bak", ".json.bak.move-tmp", ".json.tmp", ".json.tmp.move-tmp"}) {
    const size_t size = strlen(suffix);
    if (base + size >= GUARD_BUFFER_BYTES) return DestinationGuardResult::Unavailable;
    memcpy(buffer.get() + base, suffix, size + 1);
    if (!check.inspect()) return check.result;
  }
  return storage.mutationSnapshotUnchanged(check.snapshot) ? DestinationGuardResult::Clear
                                                           : DestinationGuardResult::Changed;
}
}  // namespace syncpublish
