#pragma once

// Adapted from CrossInk 9914146; attribution and license in docs/downstream/CROSSINK_NOTICE.md.

// Bounded-memory sorting for the Library builder.
//
// Metadata orders use a fixed run buffer and spill larger inputs to the card.
// Full folded names and series positions are compared in 12-byte segments.
// The caller owns the staged metadata and receives sorted record ordinals.

#include <HalStorage.h>
#include <Memory.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace library {

inline constexpr size_t SORT_SEGMENT_BYTES = 12;

struct SortEntry {
  char key[SORT_SEGMENT_BYTES];
  // Breaks ties, and is the value most passes emit (a title position).
  uint16_t ordinal;
  // The staged book whose text the key came from; passed back to the loader.
  uint16_t source;
};
static_assert(sizeof(SortEntry) == 16, "sort entries must tile a 512-byte sector");

// Write key bytes [offset, offset + 12) for `source` into `segment`, zero-padded
// past the end of the value, and set `valueBytes` to the whole key's length.
// Refinement stops once every compared value has ended, so equal values (a
// shared genre, say) cost one load each rather than one per segment of the
// maximum key length. Return false on I/O failure.
using SortSegmentLoader = bool (*)(void* context, uint16_t source, size_t offset, char* segment, size_t& valueBytes);
// Receives entries in sorted order. Return false to abort the sort.
using SortEmitter = bool (*)(void* context, const SortEntry& entry);

struct SortConfig {
  const char* runPath = nullptr;
  // Full key length. Keys longer than one segment need `load`.
  size_t keyBytes = SORT_SEGMENT_BYTES;
  SortSegmentLoader load = nullptr;
  void* loadContext = nullptr;
  // Called between bounded units of work; `stopped` aborts the sort.
  void (*service)() = nullptr;
  bool (*stopped)() = nullptr;
};

// The shared run buffer is at most 32 KiB. At the 4096-book ceiling, the
// minimum 4 KiB buffer produces at most 16 runs.
inline constexpr uint16_t SORT_RUN_ENTRIES = 2048;
// Allocation retries halve the buffer down to this before giving up.
inline constexpr uint16_t SORT_MIN_RUN_ENTRIES = 256;

// Tests only: cap the in-RAM buffer to force spilled runs. 0 restores the default.
void setSortRunCapacityForTesting(uint16_t entries);

// Fully order `count` entries in place: by key prefix, then by refined key
// segments for equal prefixes, then by ordinal. Prefixes are left intact.
bool sortEntries(SortEntry* entries, uint16_t count, const SortConfig& config);

class ExternalSorter {
 public:
  ExternalSorter() = default;
  ~ExternalSorter();
  ExternalSorter(const ExternalSorter&) = delete;
  ExternalSorter& operator=(const ExternalSorter&) = delete;

  // `expected` sizes the buffer; adding more entries than that is allowed.
  bool begin(const SortConfig& config, uint16_t expected);
  bool add(const SortEntry& entry);
  // Emits every added entry in order, then releases the buffer and run file.
  bool finish(SortEmitter emit, void* context);

  bool spilled() const { return runCount > 0; }

 private:
  struct Head;
  bool spillRun();
  bool merge(SortEmitter emit, void* context);
  const char* segmentAt(Head& head, size_t offset, char* scratch);
  int compare(Head& a, Head& b);
  void release();

  SortConfig config;
  std::unique_ptr<SortEntry[]> buffer;
  uint16_t capacity = 0;
  uint16_t used = 0;
  uint16_t runCount = 0;
  uint32_t total = 0;
  HalFile runs;
  bool failed = false;
};

}  // namespace library
