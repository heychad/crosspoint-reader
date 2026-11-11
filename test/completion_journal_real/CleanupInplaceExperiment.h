#pragma once

// Test-only experiment: reuse a fixed marker file. This does not handle move
// records, physical card identity, arbitrary aliases or concurrent writers.
#include "CleanupExperiment.h"

namespace cleanupinplace {
namespace ce = cleanupexperiment;
namespace cj = readingstats::completionjournal;
using Result = ce::Result;

inline Result reclaim(unsigned slot) {
  if (slot >= 64) return Result::Refused;
  char recordPath[64], sealPath[64], receiptPath[64], markerPath[64], movePath[64], moveSealPath[64];
  ce::path(slot, ".completion-", recordPath);
  ce::path(slot, ".cseal-", sealPath);
  ce::path(slot, ".cretire-", receiptPath);
  ce::path(slot, ".cclean-", markerPath);
  ce::path(slot, ".cmove-", movePath);
  ce::path(slot, ".cmoveseal-", moveSealPath);
  if (Storage.lookupPath(movePath) != HalStorage::PathStatus::Missing ||
      Storage.lookupPath(moveSealPath) != HalStorage::PathStatus::Missing)
    return Result::Refused;
  cj::Record record{};
  size_t size = 0;
  const auto recordState = cj::inspectRecord(slot, record, size);
  if (recordState != cj::InspectResult::Missing && recordState != cj::InspectResult::Valid) return Result::Refused;
  cj::Seal receipt{};
  const bool fullyRetired = recordState == cj::InspectResult::Valid && !(record[9] & cj::MOVE_TO_READ) &&
                            cj::inspectSeal(slot, record, size) == cj::SealResult::Matching &&
                            ce::readExact(receiptPath, receipt.data(), receipt.size()) &&
                            cj::matchesRetirement(slot, record, size, receipt);
  const auto markerState = Storage.lookupPath(markerPath);
  if (markerState == HalStorage::PathStatus::Failed) return Result::Unavailable;
  ce::Capsule marker{};
  if (fullyRetired) {
    const auto desired = ce::capsule(slot, receipt);
    const bool already = markerState == HalStorage::PathStatus::Present &&
                         ce::readExact(markerPath, marker.data(), marker.size()) && marker == desired;
    if (!already) {
      auto file = Storage.openChecked(
          markerPath, markerState == HalStorage::PathStatus::Missing ? (O_RDWR | O_CREAT | O_EXCL) : O_RDWR);
      if (!file || file.fileSize() > desired.size() || !file.seekSet(0)) return Result::Uncertain;
      const bool written = file.write(desired.data(), desired.size()) == desired.size();
      const bool synced = file.sync(), closed = file.close();
      if (!written || !synced || !closed || !ce::readExact(markerPath, marker.data(), marker.size()) ||
          marker != desired)
        return Result::Uncertain;
    }
  } else {
    if (markerState == HalStorage::PathStatus::Missing) {
      return recordState == cj::InspectResult::Missing &&
                     Storage.lookupPath(sealPath) == HalStorage::PathStatus::Missing &&
                     Storage.lookupPath(receiptPath) == HalStorage::PathStatus::Missing
                 ? Result::Missing
                 : Result::Refused;
    }
    if (!ce::readExact(markerPath, marker.data(), marker.size()) || !ce::decode(slot, marker, receipt))
      return Result::Refused;
    if (recordState == cj::InspectResult::Valid) {
      cj::Seal existingReceipt{};
      if ((record[9] & cj::MOVE_TO_READ) || !cj::matchesRetirement(slot, record, size, receipt) ||
          !ce::readExact(receiptPath, existingReceipt.data(), existingReceipt.size()) || existingReceipt != receipt)
        return Result::Refused;
    }
  }
  if (!ce::decode(slot, marker, receipt)) return Result::Refused;
  for (const char* name : {sealPath, receiptPath}) {
    const auto status = Storage.lookupPath(name);
    if (status == HalStorage::PathStatus::Failed) return Result::Unavailable;
    if (status == HalStorage::PathStatus::Present) {
      cj::Seal found{}, expected = receipt;
      if (name == sealPath) expected[3] = 'S';
      if (!ce::readExact(name, found.data(), found.size()) || found != expected) return Result::Refused;
    }
  }
  for (const char* name : {sealPath, recordPath, receiptPath}) {
    const auto status = Storage.lookupPath(name);
    if (status == HalStorage::PathStatus::Failed) return Result::Unavailable;
    if (status == HalStorage::PathStatus::Present && !Storage.remove(name)) return Result::Uncertain;
    if (Storage.lookupPath(name) != HalStorage::PathStatus::Missing) return Result::Uncertain;
  }
  return Result::Reclaimed;
}
}  // namespace cleanupinplace
