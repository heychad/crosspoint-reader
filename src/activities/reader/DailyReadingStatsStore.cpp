// CrossInk RC daily format and snapshot strategy; see docs/downstream/CROSSINK_NOTICE.md.
#include "DailyReadingStatsStore.h"

#include <HalStorage.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include "ReadingStatsUtils.h"

namespace dailyreading {
namespace {
struct Work {
  Month selected;
  Month other;
  std::array<uint8_t, MONTH_BYTES> bytes{};
  ReadingStatsDate date;
  uint32_t day = 0;
  uint32_t first = 0;
  uint8_t count = 0;
  int slot = -1;
  Decode slots[2]{Decode::Missing, Decode::Missing};
  Counter counter;
  ReadResult result = ReadResult::Unavailable;
  bool update = false;
};
static_assert(sizeof(Work) <= 832);
void monthPath(const Work& work, int slot, char* path) {
  snprintf(path, 64, "%s/%04u-%02u.%c.bin", DIRECTORY, work.date.year, work.date.month, slot ? 'b' : 'a');
}
void legacyPath(uint32_t day, const char* suffix, char* path) {
  snprintf(path, 64, "%s/%05lu.bin%s", DIRECTORY, static_cast<unsigned long>(day), suffix);
}
Decode loadSlot(Work& work, int slot, Month& month) {
  char path[64];
  monthPath(work, slot, path);
  const auto status = Storage.lookupPath(path);
  if (status == HalStorage::PathStatus::Missing) return Decode::Missing;
  if (status == HalStorage::PathStatus::Failed) return Decode::Unreadable;
  HalFile file;
  if (!Storage.openFileForRead("DailyStats", path, file)) return Decode::Unreadable;
  const size_t size = file.fileSize();
  const size_t requested = std::min(size, work.bytes.size());
  const bool read = !file.isDirectory() && file.read(work.bytes.data(), requested) == static_cast<int>(requested);
  const bool closed = file.close();
  if (!read || !closed) return Decode::Unreadable;
  return decodeMonth(work.bytes.data(), size, work.first, work.count, month);
}
bool loadLegacy(Work& work, uint32_t day, Counter& counter, bool& present, const char* writeTarget = nullptr) {
  counter = {};
  present = false;
  bool validRecord = false;
  for (const char* suffix : {"", ".bak", ".tmp"}) {
    char path[64];
    legacyPath(day, suffix, path);
    const auto status = Storage.lookupPath(path);
    if (status == HalStorage::PathStatus::Failed) return false;
    if (status == HalStorage::PathStatus::Missing) continue;
    present = true;
    if (writeTarget && !Storage.distinctFirstClusters(writeTarget, path)) return false;
    HalFile file;
    if (!Storage.openFileForRead("DailyStats", path, file)) return false;
    const size_t size = file.fileSize();
    const size_t requested = std::min(size, size_t(LEGACY_BYTES));
    const bool read = !file.isDirectory() && file.read(work.bytes.data(), requested) == static_cast<int>(requested);
    const bool closed = file.close();
    if (!read || !closed) return false;
    Counter value;
    const auto decoded = decodeLegacy(work.bytes.data(), size, day, value);
    if (decoded == Decode::Unsupported) return false;
    if (decoded != Decode::Valid) continue;
    validRecord = true;
    counter.seconds = std::max(counter.seconds, value.seconds);
    counter.uploaded = std::max(counter.uploaded, value.uploaded);
  }
  return !present || validRecord;
}
bool saveSlot(Work& work, int target) {
  char path[64];
  monthPath(work, target, path);
  if (!encodeMonth(work.selected, work.first, work.count, work.bytes)) return false;
  HalFile file;
  if (!Storage.openFileForWrite("DailyStats", path, file)) return false;
  const bool written = file.write(work.bytes.data(), work.bytes.size()) == work.bytes.size() && file.sync();
  const bool closed = file.close();
  if (!written || !closed) return false;
  return loadSlot(work, target, work.other) == Decode::Valid && work.other == work.selected;
}
bool operate(void* raw) {
  auto& work = *static_cast<Work*>(raw);
  work.slots[0] = loadSlot(work, 0, work.selected);
  work.slots[1] = loadSlot(work, 1, work.other);
  switch (select(work.slots[0], work.selected, work.slots[1], work.other)) {
    case Selection::Unavailable:
      return false;
    case Selection::First:
      work.slot = 0;
      break;
    case Selection::Second:
      work.slot = 1;
      work.selected = work.other;
      break;
    case Selection::Empty:
      work.selected = {};
      break;
  }
  if (!work.update) {
    Counter old;
    bool present;
    if (!loadLegacy(work, work.day, old, present)) return false;
    work.counter = work.selected.days[work.day - work.first];
    work.counter.seconds = std::max(work.counter.seconds, old.seconds);
    work.counter.uploaded = std::max(work.counter.uploaded, old.uploaded);
    work.result = work.slot >= 0 || present ? ReadResult::Available : ReadResult::Missing;
    return true;
  }
  const int target = work.slot == 0 ? 1 : 0;
  char targetPath[64];
  monthPath(work, target, targetPath);
  const bool targetExists = work.slots[target] != Decode::Missing;
  if (work.slot >= 0 && targetExists) {
    char sourcePath[64];
    monthPath(work, work.slot, sourcePath);
    // Every accepted file is shorter than a FAT cluster. Shared first clusters
    // would make truncating the target destroy the surviving copy.
    if (!Storage.distinctFirstClusters(sourcePath, targetPath)) return false;
  }
  for (uint8_t i = 0; i < work.count; ++i) {
    Counter old;
    bool present;
    if (!loadLegacy(work, work.first + i, old, present, targetExists ? targetPath : nullptr)) return false;
    auto& value = work.selected.days[i];
    value.seconds = std::max(value.seconds, old.seconds);
    value.uploaded = std::max(value.uploaded, old.uploaded);
  }
  auto& previous = work.selected.days[work.day - work.first];
  if (work.counter.seconds < previous.seconds || work.counter.uploaded < previous.uploaded) return false;
  // Repeating a verified absolute value needs no additional write. An empty
  // month still needs a first snapshot, even when the requested value is zero.
  if (work.slot >= 0 && previous == work.counter) return true;
  const auto directory = Storage.lookupPath(DIRECTORY);
  if (directory == HalStorage::PathStatus::Failed ||
      (directory == HalStorage::PathStatus::Missing && !Storage.mkdir(DIRECTORY)))
    return false;
  if (directory == HalStorage::PathStatus::Present) {
    auto dir = Storage.open(DIRECTORY);
    const bool usable = dir && dir.isDirectory();
    const bool closed = dir.close();
    if (!usable || !closed) return false;
  }
  if (work.slot < 0) {
    // Keep an empty verified recovery copy before the first actual update.
    // Legacy data has already been merged and is never removed.
    if (!saveSlot(work, 1)) return false;
    work.slot = 1;
  }
  previous = work.counter;
  ++work.selected.generation;
  return saveSlot(work, 1 - work.slot);
}
std::unique_ptr<Work> prepare(uint32_t day) {
  ReadingStatsDate date;
  if (!readingStatsDateFromDayIndex(day, date)) return {};
  auto work = makeUniqueNoThrow<Work>();
  if (!work) return {};
  work->day = day;
  work->first = day - date.day + 1;
  work->date = {date.year, date.month, 1};
  work->count = daysInMonth(date.year, date.month);
  return work;
}
}  // namespace

ReadResult read(uint32_t day, Counter& result) {
  result = {};
  auto work = prepare(day);
  if (!work || !Storage.withStorageLock(operate, work.get())) return ReadResult::Unavailable;
  result = work->counter;
  return work->result;
}
bool write(uint32_t day, const Counter& counter) {
  if (!valid(counter)) return false;
  auto work = prepare(day);
  if (!work) return false;
  work->update = true;
  work->counter = counter;
  return Storage.withStorageLock(operate, work.get());
}
}  // namespace dailyreading
