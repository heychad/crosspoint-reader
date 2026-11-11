#pragma once

// Research-only journal reclamation. This is intentionally not firmware code:
// it does not cover move plans or aliases, and it assumes the same volume.
#include <CompletionJournalIO.h>
#include <HalStorage.h>

#include <array>
#include <cstdio>
#include <cstring>

namespace cleanupexperiment {
namespace cj = readingstats::completionjournal;
using Capsule = std::array<uint8_t, 40>;
enum class Result { Reclaimed, Missing, Refused, Unavailable, Uncertain };

inline void path(unsigned slot, const char* prefix, char (&out)[64]) {
  std::snprintf(out, sizeof(out), "/.completion-journal/%s%02u", prefix, slot);
}
inline Capsule capsule(unsigned slot, const cj::Seal& receipt) {
  Capsule value{'X', '4', 'C', 'L', 'E', 'A', 'N', 1};
  value[8] = static_cast<uint8_t>(slot);
  std::memcpy(value.data() + 16, receipt.data(), receipt.size());
  const auto hash = cj::checksum(value.data(), 32);
  for (size_t i = 0; i < 8; ++i) value[32 + i] = static_cast<uint8_t>(hash >> (8 * i));
  return value;
}
inline bool decode(unsigned slot, const Capsule& value, cj::Seal& receipt) {
  const Capsule magic{'X', '4', 'C', 'L', 'E', 'A', 'N', 1};
  if (slot >= 64 || std::memcmp(value.data(), magic.data(), 8) || value[8] != slot) return false;
  for (size_t i = 9; i < 16; ++i)
    if (value[i]) return false;
  cj::Seal candidate{};
  std::memcpy(candidate.data(), value.data() + 16, candidate.size());
  if (candidate[0] != 'C' || candidate[1] != 'P' || candidate[2] != 'C' || candidate[3] != 'R' || candidate[4] != 1 ||
      candidate[5] != slot || capsule(slot, candidate) != value)
    return false;
  receipt = candidate;
  return true;
}
inline bool readExact(const char* name, uint8_t* bytes, size_t size) {
  HalFile file;
  if (!Storage.openFileForRead("CLEAN", name, file)) return false;
  const bool read = file.fileSize() == size && file.read(bytes, size) == static_cast<int>(size);
  const bool closed = file.close();
  return read && closed;
}
inline Result reclaim(unsigned slot) {
  if (slot >= 64) return Result::Refused;
  char recordPath[64], sealPath[64], receiptPath[64], markerPath[64], movePath[64], moveSealPath[64];
  path(slot, ".completion-", recordPath);
  path(slot, ".cseal-", sealPath);
  path(slot, ".cretire-", receiptPath);
  path(slot, ".cclean-", markerPath);
  path(slot, ".cmove-", movePath);
  path(slot, ".cmoveseal-", moveSealPath);
  if (Storage.lookupPath(movePath) != HalStorage::PathStatus::Missing ||
      Storage.lookupPath(moveSealPath) != HalStorage::PathStatus::Missing)
    return Result::Refused;
  auto markerState = Storage.lookupPath(markerPath);
  if (markerState == HalStorage::PathStatus::Failed) return Result::Unavailable;
  cj::Seal receipt{};
  if (markerState == HalStorage::PathStatus::Missing) {
    cj::Record record{};
    size_t size = 0;
    const auto inspected = cj::inspectRecord(slot, record, size);
    if (inspected == cj::InspectResult::Missing) return Result::Missing;
    if (inspected != cj::InspectResult::Valid || record[9] & cj::MOVE_TO_READ ||
        cj::inspectSeal(slot, record, size) != cj::SealResult::Matching ||
        !readExact(receiptPath, receipt.data(), receipt.size()) || !cj::matchesRetirement(slot, record, size, receipt))
      return Result::Refused;
    const auto encoded = capsule(slot, receipt);
    auto out = Storage.openChecked(markerPath, O_WRONLY | O_CREAT | O_EXCL);
    if (!out) return Result::Uncertain;
    const bool written = out.write(encoded.data(), encoded.size()) == encoded.size();
    const bool synced = out.sync(), closed = out.close();
    if (!written || !synced || !closed) return Result::Uncertain;
  }
  Capsule marker{};
  if (!readExact(markerPath, marker.data(), marker.size()) || !decode(slot, marker, receipt)) return Result::Refused;
  // A stale capsule cannot delete a newly reused slot. Every surviving file
  // must still match the capsule before any deletion takes place.
  cj::Record record{};
  size_t size = 0;
  const auto recordState = cj::inspectRecord(slot, record, size);
  if (recordState != cj::InspectResult::Missing &&
      (recordState != cj::InspectResult::Valid || record[9] & cj::MOVE_TO_READ ||
       !cj::matchesRetirement(slot, record, size, receipt)))
    return Result::Refused;
  for (const char* name : {sealPath, receiptPath}) {
    const auto status = Storage.lookupPath(name);
    if (status == HalStorage::PathStatus::Failed) return Result::Unavailable;
    if (status == HalStorage::PathStatus::Present) {
      cj::Seal found{}, expected = receipt;
      if (name == sealPath) expected[3] = 'S';
      if (!readExact(name, found.data(), found.size()) || found != expected) return Result::Refused;
    }
  }
  for (const char* name : {sealPath, recordPath, receiptPath, markerPath}) {
    const auto status = Storage.lookupPath(name);
    if (status == HalStorage::PathStatus::Failed) return Result::Unavailable;
    if (status == HalStorage::PathStatus::Present && !Storage.remove(name)) return Result::Uncertain;
    if (Storage.lookupPath(name) != HalStorage::PathStatus::Missing) return Result::Uncertain;
  }
  return Result::Reclaimed;
}
}  // namespace cleanupexperiment
